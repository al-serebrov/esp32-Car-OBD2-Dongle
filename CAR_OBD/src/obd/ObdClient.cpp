#include "ObdClient.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include <strings.h>
#include "Config.h"
#include "logging/FieldLog.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/task.h"

namespace {
FieldLog& log() { return FieldLog::instance(); }
void addressText(const uint8_t* mac, char (&text)[18]) {
    std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}
bool checked(esp_err_t error, const char* action) {
    if (error == ESP_OK) return true;
    log().write("BT_API_ERROR action=%s error=%s", action, esp_err_to_name(error));
    return false;
}
}  // namespace

ObdClient* ObdClient::instance_ = nullptr;

bool ObdClient::begin() {
    instance_ = this;
    nvs_handle_t storage;
    if (nvs_open("diagnostics", NVS_READONLY, &storage) == ESP_OK) {
        auto& report = elm_.diagnostics().storageReport();
        size_t size = sizeof(report);
        const auto error = nvs_get_blob(storage, "latest", &report, &size);
        nvs_close(storage);
        const bool valid = elm_.diagnostics().acceptStoredReport();
        log().write("DIAG_REPORT_LOAD valid=%d bytes=%u error=%s", valid && error == ESP_OK && size == sizeof(report),
                    static_cast<unsigned>(size), esp_err_to_name(error));
    }
    elm_.diagnostics().setCandidateFocus(config::kCandidateDiagnostics);
    events_ = xQueueCreate(24, sizeof(Event));
    if (!events_) { log().write("BT_INIT_ERROR event queue allocation"); return false; }
    // No Wi-Fi or BLE stack is started. Release BLE memory for Classic SPP.
    if (!checked(esp_bt_controller_mem_release(ESP_BT_MODE_BLE), "release BLE memory")) return false;
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (!checked(esp_bt_controller_init(&controller), "controller init") ||
        !checked(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT), "controller enable")) return false;
    esp_bluedroid_config_t bluedroid = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid.ssp_en = false;  // V-LINK legacy 4-digit PIN pairing.
    if (!checked(esp_bluedroid_init_with_cfg(&bluedroid), "bluedroid init") ||
        !checked(esp_bluedroid_enable(), "bluedroid enable") ||
        !checked(esp_bt_gap_register_callback(gap), "register GAP") ||
        !checked(esp_spp_register_callback(spp), "register SPP")) return false;
    esp_bt_pin_code_t pin = {};
    std::memcpy(pin, config::kAdapterPin, sizeof(config::kAdapterPin) - 1);
    if (!checked(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, sizeof(config::kAdapterPin) - 1, pin), "set legacy PIN")) return false;
    if (xTaskCreate(task, "obd_client", 4096, this, 2, nullptr) != pdPASS) {
        log().write("BT_INIT_ERROR worker allocation"); return false;
    }
    esp_spp_cfg_t sppConfig = {};
    sppConfig.mode = ESP_SPP_MODE_CB;
    sppConfig.enable_l2cap_ertm = false;
    log().write("BT_INIT target=%s legacy PIN configured; device discovery each boot", config::kAdapterName);
    return checked(esp_spp_enhanced_init(&sppConfig), "SPP init");
}

SpeedReading ObdClient::reading() {
    portENTER_CRITICAL(&readingLock_);
    const auto result = reading_;
    portEXIT_CRITICAL(&readingLock_);
    return result;
}

void ObdClient::invalidate() {
    portENTER_CRITICAL(&readingLock_);
    reading_.valid = false;
    mil_.valid = false;
    for (auto& value : details_.values) value.valid = false;
    driving_.rpm.valid=driving_.load.valid=driving_.throttle.valid=driving_.transmission.valid=false;
    portEXIT_CRITICAL(&readingLock_);
}

DetailReadings ObdClient::details() {
    portENTER_CRITICAL(&readingLock_);
    const auto result = details_;
    portEXIT_CRITICAL(&readingLock_);
    return result;
}

EngineReadings ObdClient::engineReadings() {
    portENTER_CRITICAL(&readingLock_);
    const EngineReadings result{mil_, details_.values[static_cast<unsigned>(Metric::Coolant)],
                               details_.values[static_cast<unsigned>(Metric::Voltage)]};
    portEXIT_CRITICAL(&readingLock_);
    return result;
}
DrivingReadings ObdClient::drivingReadings() {
    portENTER_CRITICAL(&readingLock_);
    auto result=driving_;result.coolant=details_.values[1];result.voltage=details_.values[0];
    portEXIT_CRITICAL(&readingLock_);
    return result;
}

DiagnosticStatus ObdClient::diagnosticStatus() {
    portENTER_CRITICAL(&readingLock_);
    const auto result = diagnosticStatus_;
    portEXIT_CRITICAL(&readingLock_);
    return result;
}

void ObdClient::saveDiagnostics(uint64_t now, bool force) {
    auto& run = elm_.diagnostics();
    if (force) saveRequested_ = true;
    if (!run.dirty() || run.active() || writing_ || now < nextDiagnosticSaveAttempt_ ||
        (state_ == State::Open && elm_.requestPending())) return;
    const auto status = run.status();
    const bool newResult = status.tested != savedTested_;
    if (!saveRequested_ && now - lastDiagnosticSave_ < (newResult ? 2000u : 30000u)) return;
    run.prepareSave();
    nvs_handle_t storage;
    auto error = nvs_open("diagnostics", NVS_READWRITE, &storage);
    if (error == ESP_OK) {
        error = nvs_set_blob(storage, "latest", &run.report(), sizeof(run.report()));
        if (error == ESP_OK) error = nvs_commit(storage);
        nvs_close(storage);
    }
    nextDiagnosticSaveAttempt_ = now + 2000;
    if (error == ESP_OK) {
        run.saved(); lastDiagnosticSave_ = now; savedTested_ = status.tested; saveRequested_ = false;
    }
    log().write("DIAG_CHECKPOINT session=%u tested=%u bytes=%u result=%s", static_cast<unsigned>(run.report().session),
                status.tested, static_cast<unsigned>(sizeof(run.report())), esp_err_to_name(error));
}

void ObdClient::logDiagnostic() {
    const auto& run = elm_.diagnostics();
    log().write("DIAG_RESULT session=%u name=%s request=%s tx=%03X status=%s candidate=%d value=%s",
                static_cast<unsigned>(run.report().session), run.name(), run.request(), run.target(),
                obd::diagnosticOutcomeName(run.outcome()), run.candidate(), run.description());
    if (run.outcome() != obd::DiagnosticOutcome::Success) return;
    for (size_t offset = 0; offset < run.payloadLength(); offset += 32) {
        char hex[65];
        const auto size = std::min(size_t{32}, run.payloadLength() - offset);
        for (size_t i = 0; i < size; ++i) std::snprintf(hex + 2*i, 3, "%02X", run.payload()[offset+i]);
        hex[size*2] = 0;
        log().write("DIAG_DATA item=%u request=%s offset=%u bytes=%s", run.item(), run.request(), static_cast<unsigned>(offset), hex);
    }
    // Published offsets are candidates, never validated warning/gear states.
    if (!run.candidate()) return;
    const auto* data = run.payload();
    const auto speed = reading();
    const auto rpm = run.referenceRpm();
    const auto intake = run.referenceIntake();
    const auto coolant = engineReadings().coolant;
    const auto now = static_cast<uint64_t>(esp_timer_get_time()/1000);
    const auto age = [now](const auto& value) -> unsigned { return now >= value.receivedMs ? static_cast<unsigned>(now-value.receivedMs) : UINT32_MAX; };
    log().write("CANDIDATE_CONTEXT item=%u elapsed_ms=%u speed_kmh=%d speed_valid=%d speed_age_ms=%u rpm=%d rpm_valid=%d rpm_age_ms=%u",
                run.item(), static_cast<unsigned>(run.report().activeMs), speed.kmh, isLive(speed, now), age(speed),
                rpm.value, isFresh(rpm, now, 15000), age(rpm));
    log().write("CANDIDATE_CONTEXT item=%u coolant_C=%d coolant_valid=%d coolant_age_ms=%u intake_C=%d intake_valid=%d intake_age_ms=%u",
                run.item(), coolant.value, isFresh(coolant, now, config::kCoolantStaleMs), age(coolant),
                intake.value, isFresh(intake, now, 15000), age(intake));
    if (run.item() == 269 && run.payloadLength() > 22) {
        log().write("TCM_CANDIDATE gear_raw=%u commanded_raw=%u ATF_raw=%u ATF_candidate_C=%d rpm_candidate=%u speed_candidate_kmh=%u mapping_verified=0",
                    data[21], data[22], data[13], int(data[13])-40, (unsigned(data[4])*256+data[5])/4, data[6]);
    }
    if (run.item() == 280 && run.payloadLength() > 34) {
        log().write("ENGINE_CANDIDATE oil_raw=%u oil_candidate_quarter_C=%d mapping_verified=0", data[34], int(data[34])*3-192);
    }
    const auto field = [&](const char* name, size_t byte, unsigned mask = 255, unsigned shift = 0) {
        if (byte < run.payloadLength())
            log().write("DIAG_FIELD name=%s raw=%u byte=%u mask=%02X mapping_verified=0 item=%u",
                        name, (data[byte] & mask) >> shift, static_cast<unsigned>(byte), mask, run.item());
        else log().write("DIAG_FIELD name=%s status=SHORT_PAYLOAD mapping_verified=0 item=%u", name, run.item());
    };
    switch (run.item()-269) {
        case 0: case 2:
            field("gear_current", 21); field("gear_commanded", 22); field("ATF_temperature", 13);
            if (run.item() == 271) field("selector_PRND", 20);
            break;
        case 1: field("gear_current", 4); break;
        case 3: field("gear_current", 23, 15); field("gear_next", 24, 15); break;
        case 4: field("ATF_temperature", 33); break;
        case 5:
            field("parking_brake", 4, 8, 3); field("driver_belt_latched", 5, 2, 1);
            field("doors_trunk", 4); break;
        case 6: field("reverse_selected", 4, 16, 4); field("rear_belts", 6, 14, 1); break;
        case 7: field("headlight_switch", 6); field("hazard_switch", 4); break;
        case 8: field("brake_lamp", 4); break;
        case 9: field("right_turn", 4, 4, 2); field("left_turn", 4, 2, 1); break;
        case 10: field("high_beam", 6, 1); field("low_beam", 7, 1); break;
        case 11: field("oil_temperature", 34); break;
        case 12: field("fuel_liters", 4); field("odometer_km_byte0", 6); field("odometer_km_byte1", 7); field("odometer_km_byte2", 8); break;
        case 13: field("odometer_km_byte0", 57); field("odometer_km_byte1", 58); field("odometer_km_byte2", 59); field("odometer_km_byte3", 60); break;
    }
}

void ObdClient::dumpDiagnosticReport() {
    // USB task reads a committed NVS snapshot, never the worker's mutable report.
    auto* report = static_cast<obd::DiagnosticRun::Report*>(std::malloc(sizeof(obd::DiagnosticRun::Report)));
    if (!report) { std::puts("DIAG_REPORT_ERROR allocation failed"); return; }
    nvs_handle_t storage;
    auto error = nvs_open("diagnostics", NVS_READONLY, &storage);
    size_t size = sizeof(*report);
    if (error == ESP_OK) { error = nvs_get_blob(storage, "latest", report, &size); nvs_close(storage); }
    if (error != ESP_OK || size != sizeof(*report) || !obd::DiagnosticRun::validReport(*report)) {
        std::printf("DIAG_REPORT_ERROR no valid saved run error=%s\n", esp_err_to_name(error)); std::free(report); return;
    }
    flockfile(stdout); // Keep live logging from splitting the exported report lines.
    std::printf("DIAG_REPORT_BEGIN version=%u session=%u active_ms=%u sweep_complete=%d profile=%s\n", static_cast<unsigned>(report->version),
                static_cast<unsigned>(report->session), static_cast<unsigned>(report->activeMs), report->sweepComplete,
                obd::DiagnosticRun::candidateReport(*report) ? "CANDIDATES" : "DISCOVERY");
    const auto meters = report->distanceTwiceKmhMs / 7200;
    std::printf("DIAG_TRIP source=local_OBD_speed distance_m=%" PRIu64 " valid_speed_ms=%u gaps=%u max_kmh=%u average_kmh=%.2f dashboard_trip_AB=unknown\n",
                meters, static_cast<unsigned>(report->speedMs), static_cast<unsigned>(report->speedGaps), report->maxSpeed,
                report->speedMs ? report->distanceTwiceKmhMs / (2.0 * report->speedMs) : 0);
    const auto bytes = [](const char* label, const uint8_t* data, size_t count) {
        std::printf(" %s=", label);
        for (size_t i = 0; i < count; ++i) std::printf("%02X", data[i]);
    };
    if (obd::DiagnosticRun::candidateReport(*report)) {
        std::printf("DIAG_GEAR_RAW_VALUES values=");
        bool any = false;
        for (unsigned value = 0; value < 256; ++value) if (report->support[value/8] & (1u << (value%8))) {
            std::printf("%s%u", any ? "," : "", value); any = true;
        }
        if (!any) std::printf("none");
        std::puts(" mapping_verified=0");
    }
    for (unsigned id = 1; id < obd::DiagnosticRun::kEntries; ++id) {
        const auto& r = report->records[id];
        if (r.outcome == obd::DiagnosticOutcome::Untested || r.outcome == obd::DiagnosticOutcome::NotSelected) continue;
        char request[12]; obd::DiagnosticRun::entryRequest(id, request, sizeof(request));
        std::printf("DIAG_ITEM id=%u name=%s request=%s status=%s success_count=%u fail_count=%u quarantined=%u bytes=%u",
                    id, obd::DiagnosticRun::entryName(id), request, obd::diagnosticOutcomeName(r.outcome), r.successes, r.failures, r.flags & 1, r.length);
        if (r.successes) {
            const auto* data = id >= 269 ? report->customSamples[id-269] : id >= 264 ? report->extraSamples[id-264] : r.sample;
            const size_t retained = std::min(size_t(r.length), id >= 264 ? size_t{64} : size_t{8});
            bytes("last", data, retained);
            if (id >= 269) {
                bytes("first", report->customFirst[id-269], retained);
                bytes("changed_bits", report->customChanged[id-269], retained);
                std::printf(" mapping_verified=0");
            }
            if (r.length > retained) std::printf(" truncated=1");
        }
        std::putchar('\n');
    }
    std::puts("DIAG_REPORT_END"); std::fflush(stdout); funlockfile(stdout); std::free(report);
}

void ObdClient::enqueue(const Event& event) {
    auto stamped = event;
    stamped.receivedMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    if (xQueueSend(events_, &stamped, 0) != pdTRUE) ++dropped_;
}

void ObdClient::gap(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* p) {
    auto* self = instance_;
    char mac[18];
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        char name[100] = {};
        int rssi = 0;
        bool haveRssi = false;
        for (int i = 0; i < p->disc_res.num_prop; ++i) {
            const auto& prop = p->disc_res.prop[i];
            if (prop.type == ESP_BT_GAP_DEV_PROP_RSSI && prop.len >= 1) {
                rssi = *static_cast<int8_t*>(prop.val); haveRssi = true;
            } else if (prop.type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                const auto length = std::min(static_cast<size_t>(prop.len), sizeof(name) - 1);
                std::memcpy(name, prop.val, length); name[length] = 0;
            } else if (prop.type == ESP_BT_GAP_DEV_PROP_EIR && !name[0]) {
                uint8_t length = 0;
                auto* bytes = esp_bt_gap_resolve_eir_data(static_cast<uint8_t*>(prop.val), ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &length);
                if (!bytes) bytes = esp_bt_gap_resolve_eir_data(static_cast<uint8_t*>(prop.val), ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &length);
                if (bytes) std::memcpy(name, bytes, std::min(static_cast<size_t>(length), sizeof(name) - 1));
            }
        }
        // Names are untrusted radio input; keep log records single-line.
        for (auto& c : name) if (c && (static_cast<unsigned char>(c) < 32 || c == 127)) c = '?';
        addressText(p->disc_res.bda, mac);
        log().write("DISCOVER mac=%s rssi=%s%d name=\"%s\"", mac, haveRssi ? "" : "unknown/", rssi, name[0] ? name : "<not advertised>");
        if (strcasecmp(name, config::kAdapterName) == 0) {
            Event found{Kind::Found}; std::memcpy(found.address, p->disc_res.bda, 6); self->enqueue(found);
        }
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        log().write("BT_DISCOVERY state=%s", p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED ? "started" : "stopped");
        if (p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) self->enqueue(Event{Kind::ScanDone});
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        addressText(p->auth_cmpl.bda, mac);
        log().write("BT_AUTH mac=%s status=%d", mac, p->auth_cmpl.stat);
        if (p->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS) self->enqueue(Event{Kind::AuthFailed});
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        addressText(p->pin_req.bda, mac);
        log().write("BT_PIN_REQUEST mac=%s requires16=%d", mac, p->pin_req.min_16_digit);
        esp_bt_pin_code_t pin = {};
        std::memcpy(pin, config::kAdapterPin, sizeof(config::kAdapterPin) - 1);
        checked(esp_bt_gap_pin_reply(p->pin_req.bda, !p->pin_req.min_16_digit,
                                    sizeof(config::kAdapterPin) - 1, pin), "PIN reply");
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        log().write("BT_UNEXPECTED_SSP_REQUEST rejected (legacy PIN configured)");
        checked(esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, false), "reject SSP");
        break;
    default: break;
    }
}

void ObdClient::spp(esp_spp_cb_event_t event, esp_spp_cb_param_t* p) {
    auto* self = instance_;
    Event message{Kind::Init};
    switch (event) {
    case ESP_SPP_INIT_EVT:
        message.status = p->init.status; break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        message.kind = Kind::Services; message.status = p->disc_comp.status;
        message.length = p->disc_comp.scn_num;
        for (int i = 0; i < p->disc_comp.scn_num; ++i)
            log().write("BT_SERVICE channel=%u name=%.100s", p->disc_comp.scn[i], p->disc_comp.service_name[i] ? p->disc_comp.service_name[i] : "<unnamed>");
        if (p->disc_comp.scn_num) message.data[0] = p->disc_comp.scn[0];
        break;
    case ESP_SPP_CL_INIT_EVT:
        message.kind = Kind::Opening; message.status = p->cl_init.status; message.handle = p->cl_init.handle; break;
    case ESP_SPP_OPEN_EVT:
        message.kind = Kind::Open; message.status = p->open.status; message.handle = p->open.handle;
        std::memcpy(message.address, p->open.rem_bda, 6); break;
    case ESP_SPP_CLOSE_EVT:
        message.kind = Kind::Closed; message.status = p->close.status; message.handle = p->close.handle;
        message.length = p->close.port_status; message.flag = p->close.async; break;
    case ESP_SPP_DATA_IND_EVT:
        message.kind = Kind::Data; message.handle = p->data_ind.handle;
        for (size_t offset = 0; offset < p->data_ind.len; offset += sizeof(message.data)) {
            message.length = std::min(sizeof(message.data), p->data_ind.len - offset);
            std::memcpy(message.data, p->data_ind.data + offset, message.length); self->enqueue(message);
        }
        return;
    case ESP_SPP_WRITE_EVT:
        message.kind = Kind::Write; message.handle = p->write.handle; message.status = p->write.status;
        message.length = p->write.len; message.flag = p->write.cong; break;
    case ESP_SPP_CONG_EVT:
        message.kind = Kind::Congested; message.handle = p->cong.handle; message.flag = p->cong.cong; break;
    default: return;
    }
    self->enqueue(message);
}

void ObdClient::retry(const char* reason, uint64_t now) {
    if (elm_.diagnostics().active()) { elm_.diagnostics().abort(obd::DiagnosticOutcome::Cancelled, now); logDiagnostic(); }
    elm_.diagnostics().breakSpeed();
    invalidate();
    if (state_ == State::Scan) checked(esp_bt_gap_cancel_discovery(), "cancel discovery");
    if (handle_) { checked(esp_spp_disconnect(handle_), "disconnect"); handle_ = 0; }
    const auto delay = std::min(30'000u, 3000u << std::min(failures_, 4u));
    ++failures_; state_ = State::Backoff; deadline_ = now + delay;
    writing_ = congested_ = false;
    saveDiagnostics(now, true);
    log().write("BT_RETRY reason=%s delay_ms=%u", reason, delay);
}

void ObdClient::handle(const Event& event, uint64_t now) {
    char mac[18];
    switch (event.kind) {
    case Kind::Init:
        log().write("BT_SPP_INIT status=%d", event.status);
        if (event.status == ESP_SPP_SUCCESS) {
            checked(esp_bt_gap_set_device_name("ESP32-OBD-Dashboard"), "set local name");
            checked(esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE), "set scan mode");
            state_ = State::Backoff; deadline_ = now;
        }
        break;
    case Kind::Found:
        if (state_ == State::Scan && !candidate_) {
            candidate_ = true; std::memcpy(target_, event.address, 6);
            addressText(target_, mac); log().write("BT_TARGET name=%s mac=%s", config::kAdapterName, mac);
        }
        break;
    case Kind::ScanDone:
        if (state_ != State::Scan) break;
        if (!candidate_) { state_ = State::Backoff; retry("V-LINK not found", now); break; }
        addressText(target_, mac); log().write("BT_SERVICE_DISCOVERY mac=%s", mac);
        state_ = State::Services; deadline_ = now + 15'000;
        if (!checked(esp_spp_start_discovery(target_), "discover SPP services")) retry("service discovery start failed", now);
        break;
    case Kind::Services:
        if (state_ != State::Services) break;
        if (event.status != ESP_SPP_SUCCESS || event.length == 0) { retry("no SPP service", now); break; }
        addressText(target_, mac);
        log().write("BT_CONNECT mac=%s channel=%u authenticated legacy pairing", mac, event.data[0]);
        state_ = State::Connecting; deadline_ = now + 20'000;
        if (!checked(esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER, event.data[0], target_), "connect SPP"))
            retry("SPP connect rejected", now);
        break;
    case Kind::Opening:
        log().write("BT_OPENING status=%d handle=%u", event.status, static_cast<unsigned>(event.handle));
        if (state_ == State::Connecting) {
            handle_ = event.handle;
            if (event.status != ESP_SPP_SUCCESS) retry("SPP opening failed", now);
        }
        break;
    case Kind::Open:
        addressText(event.address, mac);
        log().write("BT_OPEN mac=%s status=%d handle=%u", mac, event.status, static_cast<unsigned>(event.handle));
        if (state_ != State::Connecting) {
            if (event.status == ESP_SPP_SUCCESS) checked(esp_spp_disconnect(event.handle), "close late connection");
            break;
        }
        if (event.status != ESP_SPP_SUCCESS) { retry("SPP connection failed", now); break; }
        handle_ = event.handle; state_ = State::Open; writing_ = congested_ = false;
        elm_.reset(now + 300); break;
    case Kind::Closed:
        log().write("BT_CLOSED handle=%u status=%d port_status=%d remote=%d", static_cast<unsigned>(event.handle), event.status, event.length, event.flag);
        if (event.handle == handle_ && (state_ == State::Open || state_ == State::Connecting)) {
            handle_ = 0; retry("Bluetooth disconnected", now);
        }
        break;
    case Kind::Data: {
        if (state_ != State::Open || event.handle != handle_) break;
        const bool compactSpeed = config::kCandidateDiagnostics && elm_.diagnostics().enabled() && elm_.speedTransaction();
        if (!compactSpeed) log().bytes("RX", event.data, event.length);
        for (int i = 0; i < event.length; ++i) {
            const auto result = elm_.receive(static_cast<char>(event.data[i]), now);
            if (result == obd::Elm327::Result::Speed) {
                portENTER_CRITICAL(&readingLock_);
                reading_ = {elm_.speed(), event.receivedMs, true};
                portEXIT_CRITICAL(&readingLock_);
                failures_ = 0;
                elm_.diagnostics().observeSpeed(elm_.speed(), event.receivedMs);
                log().write("SPEED valid=1 kmh=%d PID=010D", elm_.speed());
            } else if (result == obd::Elm327::Result::Diagnostic) {
                logDiagnostic();
            } else if(result==obd::Elm327::Result::Driving || result==obd::Elm327::Result::DrivingError) {
                const bool valid=result==obd::Elm327::Result::Driving;
                const int id=elm_.drivingMetric();
                portENTER_CRITICAL(&readingLock_);
                if(id==3) {
                    if(valid)driving_.transmission=elm_.transmissionReading();
                    else driving_.transmission.valid=false;
                } else if(id>=0 && id<3) {
                    auto& target=id==0?driving_.rpm:id==1?driving_.load:driving_.throttle;
                    if(valid)target={elm_.value(),event.receivedMs,true};else target.valid=false;
                }
                portEXIT_CRITICAL(&readingLock_);
                if(id==3 && valid)log().write("DRIVING_GEAR selector_raw=%d gear_raw=%d tx=7E1 request=21A0",elm_.transmissionReading().selector,elm_.transmissionReading().gear);
                else log().write("DRIVING_DATA metric=%d valid=%d value=%d reason=%s",id,valid,elm_.value(),elm_.reason());
            } else if (result == obd::Elm327::Result::Detail) {
                portENTER_CRITICAL(&readingLock_);
                details_.values[elm_.metric()] = {elm_.value(), event.receivedMs, true};
                portEXIT_CRITICAL(&readingLock_);
                constexpr const char* names[] = {"adapter_mV", "coolant_C", "lambda1_mV", "lambda2_mV"};
                log().write("DETAIL metric=%s value=%d valid=1", names[elm_.metric()], elm_.value());
            } else if (result == obd::Elm327::Result::Monitor || result == obd::Elm327::Result::MonitorError) {
                const bool valid = result == obd::Elm327::Result::Monitor;
                portENTER_CRITICAL(&readingLock_);
                auto& target = elm_.monitor() == 0 ? mil_ : details_.values[static_cast<unsigned>(
                    elm_.monitor() == 1 ? Metric::Coolant : Metric::Voltage)];
                if (valid) target = {elm_.value(), event.receivedMs, true};
                else target.valid = false;
                portEXIT_CRITICAL(&readingLock_);
                if (!valid) log().write("MONITOR_ERROR request=%s reason=%s speed_invalidated=0",
                                       elm_.monitor() == 0 ? "0101" : elm_.monitor() == 1 ? "0105" : "ATRV", elm_.reason());
                else if (elm_.monitor() == 0)
                    log().write("MIL valid=1 commanded_on=%d max_reported_dtc_count=%d response_lines=%u PID=0101",
                                !!(elm_.value() & 0x80), elm_.value() & 0x7f, elm_.milResponseLines());
                else if (elm_.monitor() == 1) log().write("COOLANT valid=1 celsius=%d PID=0105", elm_.value());
                else log().write("VOLTAGE valid=1 millivolts=%d source=ATRV", elm_.value());
            } else if (result == obd::Elm327::Result::BodySample) {
                const auto* data = elm_.bodyData();
                log().write("BODY_CANDIDATE tx=770 rx=778 DID=BC03 data=%02X%02X%02X%02X%02X%02X%02X%02X mapping_verified=0",
                            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
                log().write("BODY_CANDIDATE handbrake_bit=%d driver_belt_latched_bit=%d mapping_verified=0 display_updated=0",
                            !!(data[4] & 0x08), !!(data[5] & 0x02));
            } else if (result == obd::Elm327::Result::BodyError) {
                log().write("BODY_PROBE_ERROR reason=%s display_updated=0 connection_alive=%d", elm_.reason(), elm_.alive());
                if (elm_.bodyProbeSuppressed()) log().write("BODY_PROBE_DISABLED until reboot after routing failure; restoring speed via reconnect");
            } else if (result == obd::Elm327::Result::Supported || result == obd::Elm327::Result::Unsupported) {
                log().write("PID_SUPPORT speed=%s; next request=010D", result == obd::Elm327::Result::Supported ? "yes" : "not advertised (probing)");
            } else if (result == obd::Elm327::Result::Error) {
                if (compactSpeed) log().bytes("RX_SPEED_ERROR", reinterpret_cast<const uint8_t*>(elm_.response()), elm_.pendingBytes());
                log().write("ELM_ERROR metric=%d reason=%s", elm_.metric(), elm_.reason());
                portENTER_CRITICAL(&readingLock_);
                if (elm_.metric() >= 0) details_.values[elm_.metric()].valid = false;
                else reading_.valid = false;
                portEXIT_CRITICAL(&readingLock_);
                if (elm_.metric() < 0) { elm_.diagnostics().breakSpeed(); saveDiagnostics(now, true); }
            }
        }
        if (!elm_.alive()) retry(elm_.reason(), now);
        break;
    }
    case Kind::Write:
        if (event.handle != handle_ || state_ != State::Open) break;
        writing_ = false; congested_ = event.flag;
        if (congested_) transportDeadline_ = now + 3000;
        if (event.status != ESP_SPP_SUCCESS || event.length != writeLength_) {
            log().write("BT_WRITE_ERROR status=%d written=%d expected=%d", event.status, event.length, writeLength_);
            retry("SPP write failed/partial", now);
        }
        break;
    case Kind::Congested:
        log().write("BT_CONGESTION handle=%u active=%d", static_cast<unsigned>(event.handle), event.flag);
        if (event.handle == handle_) {
            congested_ = event.flag;
            if (congested_) transportDeadline_ = now + 3000;
        }
        break;
    case Kind::AuthFailed:
        if (state_ == State::Connecting) retry("pairing/authentication failed", now);
        break;
    }
}

void ObdClient::tick(uint64_t now) {
    const bool diagnostics = diagnosticsActive_.load();
    if (diagnostics != elm_.diagnostics().enabled()) {
        elm_.diagnostics().setEnabled(diagnostics, now);
        log().write("DIAG_MODE enabled=%d page=4", diagnostics);
        if (diagnostics && config::kCandidateDiagnostics)
            log().write("DIAG_LOG_POLICY speed_values=all speed_wire=errors_only candidate_wire=all profile=CANDIDATES");
        if (!diagnostics) saveRequested_ = true;
    }
    elm_.diagnostics().tick(now, state_ == State::Open);
    elm_.setDetailsActive(detailsActive_.load());
    elm_.setDrivingActive(drivingActive_.load());
    elm_.setEngineMonitorEnabled(config::kEngineMonitorEnabled);
    elm_.setBodyProbeEnabled(config::kHyundaiBodyProbeEnabled);
    const auto lost = dropped_.exchange(0);
    if (lost) { log().write("BT_EVENT_QUEUE_OVERFLOW dropped=%u", lost); retry("lost Bluetooth events", now); }
    if (state_ == State::Backoff && now >= deadline_) {
        candidate_ = false; state_ = State::Scan; deadline_ = now + 15'000;
        log().write("BT_SCAN_START duration_units=8 target=%s", config::kAdapterName);
        if (!checked(esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0), "start discovery")) retry("scan start failed", now);
    } else if (state_ == State::Open) {
        if ((writing_ || congested_) && now >= transportDeadline_) {
            retry("SPP write/congestion stalled", now);
        } else if (elm_.timedOut(now)) {
            if (elm_.diagnosticTransaction()) logDiagnostic();
            log().bytes("RX_INCOMPLETE", reinterpret_cast<const uint8_t*>(elm_.response()), elm_.pendingBytes());
            if (elm_.bodyProbeSuppressed()) log().write("BODY_PROBE_DISABLED until reboot after timeout; restoring speed via reconnect");
            retry(elm_.reason(), now);
        } else if (!writing_ && !congested_) {
            if (const char* command = elm_.nextCommand(now)) {
                writeLength_ = std::strlen(command); writing_ = true;
                transportDeadline_ = now + 3000;
                if (!(config::kCandidateDiagnostics && elm_.diagnostics().enabled() && elm_.speedTransaction()))
                    log().bytes("TX", reinterpret_cast<const uint8_t*>(command), writeLength_);
                if (!checked(esp_spp_write(handle_, writeLength_, reinterpret_cast<uint8_t*>(const_cast<char*>(command))), "SPP write"))
                    retry("SPP write rejected", now);
            }
        }
    } else if ((state_ == State::Scan || state_ == State::Services || state_ == State::Connecting) && now >= deadline_) {
        retry("Bluetooth stage deadline exceeded", now);
    }
    saveDiagnostics(now);
    portENTER_CRITICAL(&readingLock_);
    diagnosticStatus_ = elm_.diagnostics().status();
    portEXIT_CRITICAL(&readingLock_);
}

void ObdClient::task(void* context) {
    auto* self = static_cast<ObdClient*>(context);
    Event event{Kind::Init};
    uint64_t lastHealth = 0;
    for (;;) {
        if (xQueueReceive(self->events_, &event, pdMS_TO_TICKS(20)) == pdTRUE)
            self->handle(event, static_cast<uint64_t>(esp_timer_get_time() / 1000));
        const auto now = static_cast<uint64_t>(esp_timer_get_time() / 1000);
        self->tick(now);
        if (now - lastHealth >= 30000) {
            log().write("OBD_HEALTH stack_free_bytes=%u queue_pending=%u", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                        static_cast<unsigned>(uxQueueMessagesWaiting(self->events_)));
            lastHealth = now;
        }
    }
}

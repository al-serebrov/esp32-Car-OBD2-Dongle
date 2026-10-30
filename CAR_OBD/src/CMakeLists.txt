idf_component_register(
    SRCS "main.cpp" "display/DisplayManager.cpp" "display/GaugeRenderer.cpp" "display/DrivingPageRenderer.cpp" "display/Backlight.cpp" "input/Button.cpp"
         "obd/Elm327.cpp" "obd/HyundaiBody.cpp" "obd/DiagnosticRun.cpp" "obd/TransmissionPoll.cpp" "obd/ObdClient.cpp" "logging/FieldLog.cpp" "logging/FlashJournal.cpp"
    INCLUDE_DIRS "." "../include"
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc esp_timer bt nvs_flash esp_partition esp_app_format
)

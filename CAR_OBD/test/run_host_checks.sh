#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/obd-gauge-checks.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$project_dir/include" -I"$project_dir/src" \
    "$project_dir/test/host/gauge_checks.cpp" \
    "$project_dir/src/display/GaugeRenderer.cpp" "$project_dir/src/display/DrivingPageRenderer.cpp" \
    -o "$test_dir/gauge_checks"
"$test_dir/gauge_checks" "$@"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$project_dir/include" -I"$project_dir/src" \
    "$project_dir/test/host/ui_checks.cpp" "$project_dir/src/display/GaugeRenderer.cpp" "$project_dir/src/display/DrivingPageRenderer.cpp" \
    -o "$test_dir/ui_checks"
"$test_dir/ui_checks"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$project_dir/include" -I"$project_dir/src" \
    "$project_dir/test/host/adaptive_speed_checks.cpp" "$project_dir/src/display/GaugeRenderer.cpp" "$project_dir/src/display/DrivingPageRenderer.cpp" \
    -o "$test_dir/adaptive_speed_checks"
"$test_dir/adaptive_speed_checks"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$project_dir/include" -I"$project_dir/src" \
    "$project_dir/test/host/obd_checks.cpp" "$project_dir/src/obd/Elm327.cpp" "$project_dir/src/obd/HyundaiBody.cpp" "$project_dir/src/obd/DiagnosticRun.cpp" "$project_dir/src/obd/TransmissionPoll.cpp" \
    -o "$test_dir/obd_checks"
"$test_dir/obd_checks"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$project_dir/include" -I"$project_dir/src" \
    "$project_dir/test/host/diagnostic_checks.cpp" "$project_dir/src/obd/Elm327.cpp" \
    "$project_dir/src/obd/HyundaiBody.cpp" "$project_dir/src/obd/DiagnosticRun.cpp" "$project_dir/src/obd/TransmissionPoll.cpp" \
    -o "$test_dir/diagnostic_checks"
"$test_dir/diagnostic_checks"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$project_dir/src" "$project_dir/test/host/journal_checks.cpp" \
    "$project_dir/src/logging/FlashJournal.cpp" -o "$test_dir/journal_checks"
"$test_dir/journal_checks"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$project_dir/include" -I"$project_dir/src" \
    "$project_dir/test/host/driving_checks.cpp" "$project_dir/src/display/GaugeRenderer.cpp" "$project_dir/src/display/DrivingPageRenderer.cpp" \
    "$project_dir/src/obd/Elm327.cpp" "$project_dir/src/obd/DiagnosticRun.cpp" "$project_dir/src/obd/HyundaiBody.cpp" "$project_dir/src/obd/TransmissionPoll.cpp" \
    -o "$test_dir/driving_checks"
"$test_dir/driving_checks"

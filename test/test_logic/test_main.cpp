// Unit tests for the pure logic in src/main.cpp
// ---------------------------------------------------------------------------
// Run standalone:  g++ -std=c++17 -o /tmp/test_logic test/test_logic/test_main.cpp && /tmp/test_logic
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <iostream>

// ---------------------------------------------------------------------------
// Minimal Arduino String shim
// ---------------------------------------------------------------------------
class String {
    std::string s;
public:
    String() {}
    String(const char* c) : s(c ? c : "") {}
    String(const std::string& c) : s(c) {}
    String(int n) : s(std::to_string(n)) {}

    unsigned int length() const { return s.length(); }
    bool isEmpty() const { return s.empty(); }

    String substring(unsigned int from) const {
        if (from >= s.length()) return String("");
        return String(s.substr(from));
    }
    String substring(unsigned int from, unsigned int to) const {
        if (from >= s.length() || to < from) return String("");
        return String(s.substr(from, to - from));
    }

    long toInt() const { return s.empty() ? 0 : atol(s.c_str()); }
    const char* c_str() const { return s.c_str(); }

    void trim() {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) { s = ""; return; }
        size_t end = s.find_last_not_of(" \t\r\n");
        s = s.substr(start, end - start + 1);
    }

    int indexOf(char c, int from = 0) const {
        size_t pos = s.find(c, (size_t)from);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(const char* str, int from = 0) const {
        size_t pos = s.find(str, (size_t)from);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    bool operator==(const char* other) const { return s == other; }
    bool operator==(const String& other) const { return s == other.s; }
    bool operator!=(const char* other) const { return s != other; }
    String operator+(const String& other) const { return String(s + other.s); }
    String operator+(const char* other) const { return String(s + other); }
};

// ---------------------------------------------------------------------------
// Structs and globals mirrored from main.cpp
// ---------------------------------------------------------------------------
struct ScheduleItem {
    String startTime;
    String endTime;
    String value;
    String outValue;
};

ScheduleItem schedules[10];
int scheduleCount = 0;

// ---------------------------------------------------------------------------
// Functions under test (copied verbatim from src/main.cpp)
// ---------------------------------------------------------------------------
String convertSetupValue(const String& input) {
    if (input.length() != 8) return "";
    int pset = input.substring(0, 4).toInt();
    int vset = input.substring(4, 8).toInt();
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "*%d@%d#", pset, vset);
    return String(buffer);
}

bool isTimeInRange(const String& currentTime, const String& startTime, const String& endTime) {
    int currentMinutes = (currentTime.substring(0, 2).toInt() * 60) + currentTime.substring(3, 5).toInt();
    int startMinutes   = (startTime.substring(0, 2).toInt() * 60)   + startTime.substring(3, 5).toInt();

    int endMinutes;
    if (endTime.substring(0, 2).toInt() >= 24) {
        int adjustedHour = endTime.substring(0, 2).toInt() - 24;
        endMinutes = (adjustedHour * 60) + endTime.substring(3, 5).toInt();
    } else {
        endMinutes = (endTime.substring(0, 2).toInt() * 60) + endTime.substring(3, 5).toInt();
    }

    if (endMinutes < startMinutes)
        return currentMinutes >= startMinutes || currentMinutes <= endMinutes;
    return currentMinutes >= startMinutes && currentMinutes <= endMinutes;
}

void parseScheduleData(const String& scheduleData) {
    for (int i = 0; i < 10; i++) {
        schedules[i].startTime = "";
        schedules[i].endTime   = "";
        schedules[i].value     = "";
        schedules[i].outValue  = "";
    }
    scheduleCount = 0;

    if (scheduleData.isEmpty() || scheduleData == "null") return;

    int startIndex = 0;
    while (startIndex < (int)scheduleData.length() && scheduleCount < 10) {
        int endIndex = scheduleData.indexOf('#', startIndex);
        if (endIndex == -1) endIndex = scheduleData.length();

        String schedule = scheduleData.substring(startIndex, endIndex);

        int startPos = schedule.indexOf("start=");
        int endPos   = schedule.indexOf("&", startPos);
        if (startPos != -1) {
            schedules[scheduleCount].startTime = schedule.substring(startPos + 6, endPos);
            schedules[scheduleCount].startTime.trim();
        }

        startPos = schedule.indexOf("end=");
        endPos   = schedule.indexOf("&", startPos);
        if (startPos != -1) {
            schedules[scheduleCount].endTime = schedule.substring(startPos + 4, endPos);
            schedules[scheduleCount].endTime.trim();
        }

        startPos = schedule.indexOf("value=");
        if (startPos != -1) {
            schedules[scheduleCount].value = schedule.substring(startPos + 6);
            schedules[scheduleCount].value.trim();
        }

        scheduleCount++;
        startIndex = endIndex + 1;
    }
}

// Simulates currentScheduleValue() without the NTP dependency.
String getScheduleForTime(const String& currentTime) {
    for (int i = 0; i < scheduleCount; i++) {
        if (schedules[i].startTime.isEmpty()) continue;
        if (isTimeInRange(currentTime, schedules[i].startTime, schedules[i].endTime))
            return convertSetupValue(schedules[i].value);
    }
    return "";
}

// ---------------------------------------------------------------------------
// Tiny test harness
// ---------------------------------------------------------------------------
static int g_failures = 0;
static int g_checks   = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) { g_failures++;                                          \
            std::cout << "  [FAIL] " << msg << "  (" #cond ")\n"; }          \
        else { std::cout << "  [ok]   " << msg << "\n"; }                    \
    } while (0)

#define CHECK_STR(actual, expected, msg)                                      \
    do {                                                                      \
        g_checks++;                                                           \
        String _a = (actual);                                                 \
        if (!(_a == (expected))) { g_failures++;                              \
            std::cout << "  [FAIL] " << msg << "  expected \"" << expected    \
                      << "\" got \"" << _a.c_str() << "\"\n"; }              \
        else { std::cout << "  [ok]   " << msg << "\n"; }                    \
    } while (0)

// ---------------------------------------------------------------------------
// convertSetupValue tests
// ---------------------------------------------------------------------------
void test_convertSetupValue() {
    std::cout << "convertSetupValue:\n";
    CHECK_STR(convertSetupValue("99001620"), "*9900@1620#", "typical 8-digit input");
    CHECK_STR(convertSetupValue("00120005"), "*12@5#",      "leading zeros stripped");
    CHECK_STR(convertSetupValue("00000000"), "*0@0#",       "all zeros");
    CHECK_STR(convertSetupValue(""),         "",            "empty input rejected");
    CHECK_STR(convertSetupValue("1234567"),  "",            "7 chars rejected");
    CHECK_STR(convertSetupValue("123456789"),"",            "9 chars rejected");
    CHECK_STR(convertSetupValue("99999999"), "*9999@9999#", "max values");
}

// ---------------------------------------------------------------------------
// isTimeInRange tests
// ---------------------------------------------------------------------------
void test_isTimeInRange() {
    std::cout << "isTimeInRange:\n";

    // Normal day window 08:00-17:00
    CHECK( isTimeInRange("12:00", "08:00", "17:00"), "midday inside day window");
    CHECK( isTimeInRange("08:00", "08:00", "17:00"), "start boundary inclusive");
    CHECK( isTimeInRange("17:00", "08:00", "17:00"), "end boundary inclusive");
    CHECK(!isTimeInRange("07:59", "08:00", "17:00"), "just before start excluded");
    CHECK(!isTimeInRange("17:01", "08:00", "17:00"), "just after end excluded");
    CHECK(!isTimeInRange("23:00", "08:00", "17:00"), "night outside day window");

    // Overnight window 22:00-06:00
    CHECK( isTimeInRange("23:30", "22:00", "06:00"), "late night inside overnight");
    CHECK( isTimeInRange("02:00", "22:00", "06:00"), "early morning inside overnight");
    CHECK( isTimeInRange("22:00", "22:00", "06:00"), "overnight start boundary");
    CHECK( isTimeInRange("06:00", "22:00", "06:00"), "overnight end boundary");
    CHECK(!isTimeInRange("12:00", "22:00", "06:00"), "midday outside overnight");
    CHECK(!isTimeInRange("06:01", "22:00", "06:00"), "just after overnight end excluded");

    // 24+ hour end notation: 18:00-27:00 = 18:00→03:00
    CHECK( isTimeInRange("20:00", "18:00", "27:00"), "evening inside 24+ window");
    CHECK( isTimeInRange("02:00", "18:00", "27:00"), "past-midnight inside 24+ window");
    CHECK( isTimeInRange("03:00", "18:00", "27:00"), "24+ end boundary (03:00)");
    CHECK(!isTimeInRange("04:00", "18:00", "27:00"), "after 24+ end excluded");
    CHECK(!isTimeInRange("12:00", "18:00", "27:00"), "midday outside 24+ window");
}

// ---------------------------------------------------------------------------
// parseScheduleData tests  (raw string từ production)
// ---------------------------------------------------------------------------
void test_parseScheduleData() {
    std::cout << "parseScheduleData:\n";

    const char* raw =
        "start=23:10&end=34:00&value=25101010"
        "#start=10:00&end=10:45&value=26401100"
        "#start=10:45&end=16:59&value=25601800"
        "#start=17:00&end=21:50&value=25401800"
        "#start=21:50&end=23:10&value=25301800";

    parseScheduleData(String(raw));

    CHECK(scheduleCount == 5, "5 schedule entries parsed");

    CHECK(schedules[0].startTime == "23:10", "[0] startTime");
    CHECK(schedules[0].endTime   == "34:00", "[0] endTime");
    CHECK(schedules[0].value     == "25101010", "[0] value");

    CHECK(schedules[1].startTime == "10:00", "[1] startTime");
    CHECK(schedules[1].endTime   == "10:45", "[1] endTime");
    CHECK(schedules[1].value     == "26401100", "[1] value");

    CHECK(schedules[2].startTime == "10:45", "[2] startTime");
    CHECK(schedules[2].endTime   == "16:59", "[2] endTime");
    CHECK(schedules[2].value     == "25601800", "[2] value");

    CHECK(schedules[3].startTime == "17:00", "[3] startTime");
    CHECK(schedules[3].endTime   == "21:50", "[3] endTime");
    CHECK(schedules[3].value     == "25401800", "[3] value");

    CHECK(schedules[4].startTime == "21:50", "[4] startTime");
    CHECK(schedules[4].endTime   == "23:10", "[4] endTime");
    CHECK(schedules[4].value     == "25301800", "[4] value");

    // empty / null guard
    parseScheduleData(String(""));
    CHECK(scheduleCount == 0, "empty string clears schedules");
    parseScheduleData(String("null"));
    CHECK(scheduleCount == 0, "null string clears schedules");

    parseScheduleData(String(raw)); // restore for next tests
}

// ---------------------------------------------------------------------------
// Full schedule timeline test  (raw string từ production)
// ---------------------------------------------------------------------------
void test_scheduleTimeline() {
    std::cout << "scheduleTimeline:\n";

    // Schedule 0: 23:10 → 10:00 (overnight via 34:00 notation)
    CHECK_STR(getScheduleForTime("00:00"), "*2510@1010#", "00:00 → schedule-0 overnight");
    CHECK_STR(getScheduleForTime("09:59"), "*2510@1010#", "09:59 → schedule-0 overnight");
    CHECK_STR(getScheduleForTime("23:11"), "*2510@1010#", "23:11 → schedule-0 overnight");
    CHECK_STR(getScheduleForTime("23:59"), "*2510@1010#", "23:59 → schedule-0 overnight");

    // Schedule 1: 10:00 → 10:45
    CHECK_STR(getScheduleForTime("10:01"), "*2640@1100#", "10:01 → schedule-1");
    CHECK_STR(getScheduleForTime("10:30"), "*2640@1100#", "10:30 → schedule-1");

    // Schedule 2: 10:45 → 16:59
    CHECK_STR(getScheduleForTime("10:46"), "*2560@1800#", "10:46 → schedule-2");
    CHECK_STR(getScheduleForTime("16:59"), "*2560@1800#", "16:59 → schedule-2 end boundary");

    // Schedule 3: 17:00 → 21:50
    CHECK_STR(getScheduleForTime("17:00"), "*2540@1800#", "17:00 → schedule-3 start");
    CHECK_STR(getScheduleForTime("21:49"), "*2540@1800#", "21:49 → schedule-3");

    // Schedule 4: 21:50 → 23:10
    CHECK_STR(getScheduleForTime("21:51"), "*2530@1800#", "21:51 → schedule-4");
    CHECK_STR(getScheduleForTime("23:09"), "*2530@1800#", "23:09 → schedule-4 end");

    // --- Boundary overlap: first match wins ---
    // At 10:00: schedule-0 (overnight <=600) AND schedule-1 (>=600) both match → schedule-0 wins
    CHECK_STR(getScheduleForTime("10:00"), "*2510@1010#",
              "10:00 overlap: schedule-0 beats schedule-1 (first match)");

    // At 10:45: schedule-1 AND schedule-2 both match → schedule-1 wins
    CHECK_STR(getScheduleForTime("10:45"), "*2640@1100#",
              "10:45 overlap: schedule-1 beats schedule-2 (first match)");

    // At 21:50: schedule-3 AND schedule-4 both match → schedule-3 wins
    CHECK_STR(getScheduleForTime("21:50"), "*2540@1800#",
              "21:50 overlap: schedule-3 beats schedule-4 (first match)");

    // At 23:10: schedule-4 AND schedule-0 both match → schedule-0 wins (index 0 < index 4)
    CHECK_STR(getScheduleForTime("23:10"), "*2510@1010#",
              "23:10 overlap: schedule-0 beats schedule-4 (first match)");

    // No time should return empty (full 24h coverage)
    const char* every_hour[] = {
        "00:00","01:00","02:00","03:00","04:00","05:00","06:00","07:00",
        "08:00","09:00","10:05","11:00","12:00","13:00","14:00","15:00",
        "16:00","17:30","18:00","19:00","20:00","21:00","22:00","23:00"
    };
    for (auto t : every_hour) {
        String result = getScheduleForTime(String(t));
        CHECK(result != "", std::string("full coverage at ") + t);
    }
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== main.cpp logic unit tests ===\n\n";

    test_convertSetupValue();  std::cout << "\n";
    test_isTimeInRange();      std::cout << "\n";
    test_parseScheduleData();  std::cout << "\n";
    test_scheduleTimeline();

    std::cout << "\n=== " << (g_checks - g_failures) << "/" << g_checks
              << " checks passed ===\n";
    if (g_failures > 0) {
        std::cout << g_failures << " FAILED\n";
        return 1;
    }
    std::cout << "ALL PASSED\n";
    return 0;
}

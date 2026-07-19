// Unit tests for the pure logic in src/main.cpp
// ---------------------------------------------------------------------------
// These test the hardware-independent helpers convertSetupValue() and
// isTimeInRange(). They are copied here verbatim from main.cpp (which mixes
// them with WiFi/EEPROM/MQTT code that cannot run on a host), together with a
// tiny Arduino `String` shim so the tests build and run natively.
//
// Run standalone:   g++ -std=c++17 -o /tmp/test_logic test/test_logic/test_main.cpp && /tmp/test_logic
// Or via PlatformIO: pio test -e native
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>

// ---------------------------------------------------------------------------
// Minimal Arduino String shim (only the methods used by the functions below).
// ---------------------------------------------------------------------------
class String {
    std::string s;
public:
    String() {}
    String(const char* c) : s(c) {}
    String(const std::string& c) : s(c) {}

    unsigned int length() const { return s.length(); }

    // Arduino substring(from, to) is [from, to); substring(from) goes to end.
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

    bool operator==(const char* other) const { return s == other; }
    bool operator==(const String& other) const { return s == other.s; }
};

// ---------------------------------------------------------------------------
// Functions under test (copied verbatim from src/main.cpp).
// ---------------------------------------------------------------------------
String convertSetupValue(const String& input) {
    if (input.length() != 8) {
        return "";
    }
    int pset = input.substring(0, 4).toInt();  // First 4 digits as Pset
    int vset = input.substring(4, 8).toInt();  // Last 4 digits as Vset
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "*%d@%d#", pset, vset);
    return String(buffer);
}

bool isTimeInRange(const String& currentTime, const String& startTime, const String& endTime) {
    int currentMinutes = (currentTime.substring(0, 2).toInt() * 60) + currentTime.substring(3, 5).toInt();
    int startMinutes = (startTime.substring(0, 2).toInt() * 60) + startTime.substring(3, 5).toInt();

    int endMinutes;
    if (endTime.substring(0, 2).toInt() >= 24) {
        int adjustedHour = endTime.substring(0, 2).toInt() - 24;
        endMinutes = (adjustedHour * 60) + endTime.substring(3, 5).toInt();
    } else {
        endMinutes = (endTime.substring(0, 2).toInt() * 60) + endTime.substring(3, 5).toInt();
    }

    if (endMinutes < startMinutes) {
        return currentMinutes >= startMinutes || currentMinutes <= endMinutes;
    }
    return currentMinutes >= startMinutes && currentMinutes <= endMinutes;
}

// ---------------------------------------------------------------------------
// Tiny test harness (no external deps so it runs anywhere g++ does).
// ---------------------------------------------------------------------------
static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_failures++;                                                   \
            std::cout << "  [FAIL] " << msg << "  (" #cond ")\n";           \
        } else {                                                            \
            std::cout << "  [ok]   " << msg << "\n";                        \
        }                                                                   \
    } while (0)

#define CHECK_STR(actual, expected, msg)                                    \
    do {                                                                    \
        g_checks++;                                                         \
        String _a = (actual);                                              \
        if (!(_a == (expected))) {                                          \
            g_failures++;                                                   \
            std::cout << "  [FAIL] " << msg << "  expected \"" << expected  \
                      << "\" got \"" << _a.c_str() << "\"\n";               \
        } else {                                                            \
            std::cout << "  [ok]   " << msg << "\n";                        \
        }                                                                   \
    } while (0)

// ---------------------------------------------------------------------------
// convertSetupValue tests
// ---------------------------------------------------------------------------
void test_convertSetupValue() {
    std::cout << "convertSetupValue:\n";

    // Happy path: "9900" -> Pset 9900, "1620" -> Vset 1620
    CHECK_STR(convertSetupValue("99001620"), "*9900@1620#", "typical 8-digit input");

    // Leading zeros are dropped by toInt() (documents current behaviour)
    CHECK_STR(convertSetupValue("00120005"), "*12@5#", "leading zeros stripped");

    // All zeros
    CHECK_STR(convertSetupValue("00000000"), "*0@0#", "all zeros");

    // Wrong length -> empty string
    CHECK_STR(convertSetupValue(""), "", "empty input rejected");
    CHECK_STR(convertSetupValue("1234567"), "", "7 chars rejected");
    CHECK_STR(convertSetupValue("123456789"), "", "9 chars rejected");

    // Max 4-digit values
    CHECK_STR(convertSetupValue("99999999"), "*9999@9999#", "max values");
}

// ---------------------------------------------------------------------------
// isTimeInRange tests
// ---------------------------------------------------------------------------
void test_isTimeInRange() {
    std::cout << "isTimeInRange:\n";

    // Normal same-day window 08:00 - 17:00
    CHECK(isTimeInRange("12:00", "08:00", "17:00"), "midday inside day window");
    CHECK(isTimeInRange("08:00", "08:00", "17:00"), "start boundary inclusive");
    CHECK(isTimeInRange("17:00", "08:00", "17:00"), "end boundary inclusive");
    CHECK(!isTimeInRange("07:59", "08:00", "17:00"), "just before start excluded");
    CHECK(!isTimeInRange("17:01", "08:00", "17:00"), "just after end excluded");
    CHECK(!isTimeInRange("23:00", "08:00", "17:00"), "night outside day window");

    // Overnight window 22:00 - 06:00 (endMinutes < startMinutes)
    CHECK(isTimeInRange("23:30", "22:00", "06:00"), "late night inside overnight");
    CHECK(isTimeInRange("02:00", "22:00", "06:00"), "early morning inside overnight");
    CHECK(isTimeInRange("22:00", "22:00", "06:00"), "overnight start boundary");
    CHECK(isTimeInRange("06:00", "22:00", "06:00"), "overnight end boundary");
    CHECK(!isTimeInRange("12:00", "22:00", "06:00"), "midday outside overnight");
    CHECK(!isTimeInRange("06:01", "22:00", "06:00"), "just after overnight end excluded");

    // End time expressed as 24+ hours: 18:00 - 27:00 means 18:00 -> 03:00 next day
    CHECK(isTimeInRange("20:00", "18:00", "27:00"), "evening inside 24+ window");
    CHECK(isTimeInRange("02:00", "18:00", "27:00"), "past-midnight inside 24+ window");
    CHECK(isTimeInRange("03:00", "18:00", "27:00"), "24+ end boundary (03:00)");
    CHECK(!isTimeInRange("04:00", "18:00", "27:00"), "after 24+ end excluded");
    CHECK(!isTimeInRange("12:00", "18:00", "27:00"), "midday outside 24+ window");
}

int main() {
    std::cout << "=== main.cpp logic unit tests ===\n\n";
    test_convertSetupValue();
    std::cout << "\n";
    test_isTimeInRange();

    std::cout << "\n=== " << (g_checks - g_failures) << "/" << g_checks
              << " checks passed ===\n";
    if (g_failures > 0) {
        std::cout << g_failures << " FAILED\n";
        return 1;
    }
    std::cout << "ALL PASSED\n";
    return 0;
}

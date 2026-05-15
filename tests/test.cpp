#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path &path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void writeFile(const fs::path &path, const std::string &content) {
    std::ofstream output(path);
    output << content;
}

std::string shellQuote(const fs::path &path) {
    const std::string value = path.string();

#ifdef _WIN32
    return "\"" + value + "\"";
#else
    std::string result = "'";

    for (const char ch : value) {
        if (ch == '\'') {
            result += "'\\''";
        } else {
            result += ch;
        }
    }

    result += "'";
    return result;
#endif
}

fs::path findRefactorTool() {
    if (const char *envPath = std::getenv("REFACTOR_TOOL_PATH")) {
        fs::path path = envPath;
        if (fs::exists(path)) {
            return path;
        }
    }

#ifdef REFACTOR_TOOL_PATH
    {
        fs::path path = REFACTOR_TOOL_PATH;
        if (fs::exists(path)) {
            return path;
        }
    }
#endif

#ifdef _WIN32
    const std::string toolName = "refactor_tool.exe";
#else
    const std::string toolName = "refactor_tool";
#endif

    const fs::path cwd = fs::current_path();

    const std::vector<fs::path> candidates = {
        cwd / toolName,
        cwd / "src" / toolName,
        cwd / "build" / toolName,
        cwd / "build" / "src" / toolName,

        cwd.parent_path() / toolName,
        cwd.parent_path() / "src" / toolName,
        cwd.parent_path() / "build" / toolName,
        cwd.parent_path() / "build" / "src" / toolName,

        cwd.parent_path().parent_path() / toolName,
        cwd.parent_path().parent_path() / "src" / toolName,
        cwd.parent_path().parent_path() / "build" / toolName,
        cwd.parent_path().parent_path() / "build" / "src" / toolName,
    };

    for (const auto &candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }

    ADD_FAILURE() << "refactor_tool was not found. Current directory: " << cwd.string();

    return cwd / toolName;
}

fs::path makeTempDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto *testInfo = ::testing::UnitTest::GetInstance()->current_test_info();

    fs::path dir = fs::temp_directory_path() / (std::string("refactor_tool_") + testInfo->test_suite_name() + "_" +
                                                testInfo->name() + "_" + std::to_string(now));

    fs::create_directories(dir);
    return dir;
}

std::string runRefactor(const std::string &source) {
    const fs::path tool = findRefactorTool();

    if (!fs::exists(tool)) {
        ADD_FAILURE() << "refactor_tool executable was not found: " << tool.string()
                      << "\nRun:\nfind build -type f -executable -name refactor_tool";
        return source;
    }

    const fs::path dir = makeTempDir();
    const fs::path sourcePath = dir / "case.cpp";
    const fs::path logPath = dir / "tool.log";

    writeFile(sourcePath, source);

#ifndef _WIN32
    const std::string asanEnv = "ASAN_OPTIONS=detect_leaks=0:allow_user_poisoning=0 ";
#else
    const std::string asanEnv;
#endif

    const std::string command =
        asanEnv + shellQuote(tool) + " " + shellQuote(sourcePath) + " -- -std=c++17 > " + shellQuote(logPath) + " 2>&1";

    const int exitCode = std::system(command.c_str());
    EXPECT_EQ(exitCode, 0) << readFile(logPath);

    std::string result = readFile(sourcePath);
    fs::remove_all(dir);
    return result;
}

} // namespace

TEST(VirtualDestructorRefactor, AddsVirtualToBaseDestructor) {
    const std::string input = R"cpp(
class Base {
public:
    ~Base() = default;
};

class Derived : public Base {};
)cpp";

    const std::string expected = R"cpp(
class Base {
public:
    virtual ~Base() = default;
};

class Derived : public Base {};
)cpp";

    EXPECT_EQ(runRefactor(input), expected);
}

TEST(VirtualDestructorRefactor, DoesNotChangeStandaloneOrAlreadyVirtual) {
    const std::string input = R"cpp(
class Standalone {
public:
    ~Standalone() {}
};

class Base {
public:
    virtual ~Base() {}
};

class Derived : public Base {};
)cpp";

    EXPECT_EQ(runRefactor(input), input);
}

TEST(OverrideRefactor, AddsOverrideToOverridingMethods) {
    const std::string input = R"cpp(
class Base {
public:
    virtual void f() {}
    virtual void g(int) = 0;
    virtual ~Base() {}
};

class Derived : public Base {
public:
    void f() {}
    void g(int) {}
};
)cpp";

    const std::string expected = R"cpp(
class Base {
public:
    virtual void f() {}
    virtual void g(int) = 0;
    virtual ~Base() {}
};

class Derived : public Base {
public:
    void f() override {}
    void g(int) override {}
};
)cpp";

    EXPECT_EQ(runRefactor(input), expected);
}

TEST(OverrideRefactor, DoesNotChangeExistingOverrideOrDestructor) {
    const std::string input = R"cpp(
class Base {
public:
    virtual void f() {}
    virtual ~Base() {}
};

class Derived : public Base {
public:
    void f() override {}
    ~Derived() {}
};
)cpp";

    EXPECT_EQ(runRefactor(input), input);
}

TEST(OverrideRefactor, HandlesConstRefNoexceptAndFinalSuffixes) {
    const std::string input = R"cpp(
class Base {
public:
    virtual void f() const & noexcept {}
    virtual void g() && noexcept(noexcept(1 + 1)) {}
    virtual ~Base() {}
};

class Derived : public Base {
public:
    void f() const & noexcept final {}
    void g() && noexcept(noexcept(1 + 1)) {}
};
)cpp";

    const std::string expected = R"cpp(
class Base {
public:
    virtual void f() const & noexcept {}
    virtual void g() && noexcept(noexcept(1 + 1)) {}
    virtual ~Base() {}
};

class Derived : public Base {
public:
    void f() const & noexcept override final {}
    void g() && noexcept(noexcept(1 + 1)) override {}
};
)cpp";

    EXPECT_EQ(runRefactor(input), expected);
}

TEST(OverrideRefactor, HandlesCommentsInsideMethodSignature) {
    const std::string input = R"cpp(
class Base {
public:
    virtual void f() const & noexcept {}
    virtual ~Base() {}
};

class Derived : public Base {
public:
    void f() const /**/ & /**/ noexcept /**/ {}
};
)cpp";

    const std::string expected = R"cpp(
class Base {
public:
    virtual void f() const & noexcept {}
    virtual ~Base() {}
};

class Derived : public Base {
public:
    void f() const /**/ & /**/ noexcept /**/ override {}
};
)cpp";

    EXPECT_EQ(runRefactor(input), expected);
}

TEST(RangeForRefactor, AddsReferenceToConstNonFundamentalLoopVariables) {
    const std::string input = R"cpp(
#include <iostream>
#include <string>
#include <vector>

struct Item {
    int id;
    std::string name;
};

void process() {
    std::vector<Item> items;

    for (const auto item : items) {
        std::cout << item.id;
    }

    for (const Item item : items) {
        std::cout << item.id;
    }
}
)cpp";

    const std::string expected = R"cpp(
#include <iostream>
#include <string>
#include <vector>

struct Item {
    int id;
    std::string name;
};

void process() {
    std::vector<Item> items;

    for (const auto& item : items) {
        std::cout << item.id;
    }

    for (const Item& item : items) {
        std::cout << item.id;
    }
}
)cpp";

    EXPECT_EQ(runRefactor(input), expected);
}

TEST(RangeForRefactor, DoesNotChangeFundamentalTypesOrExistingReferences) {
    const std::string input = R"cpp(
#include <iostream>
#include <string>
#include <vector>

struct Item {
    int id;
    std::string name;
};

void process() {
    std::vector<int> ints;
    std::vector<Item> items;

    for (const int value : ints) {
        std::cout << value;
    }

    for (const auto& item : items) {
        std::cout << item.id;
    }
}
)cpp";

    EXPECT_EQ(runRefactor(input), input);
}

TEST(RangeForRefactor, HandlesCommentsNearLoopVariableType) {
    const std::string input = R"cpp(
#include <iostream>
#include <string>
#include <vector>

struct Item {
    int id;
    std::string name;
};

void process() {
    std::vector<Item> items;

    for (const Item /**/ item : items) {
        std::cout << item.id;
    }
}
)cpp";

    const std::string expected = R"cpp(
#include <iostream>
#include <string>
#include <vector>

struct Item {
    int id;
    std::string name;
};

void process() {
    std::vector<Item> items;

    for (const Item& /**/ item : items) {
        std::cout << item.id;
    }
}
)cpp";

    EXPECT_EQ(runRefactor(input), expected);
}

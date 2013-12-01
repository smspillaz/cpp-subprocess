/*
 * locatebinary.cpp
 *
 * Test traversing PATH until a suitable
 * instance of a binary can be located
 *
 * See LICENSE.md for Copyright information
 */

#include <array>
#include <cstdlib>

#include <boost/tokenizer.hpp>

#include <gmock/gmock.h>

#include "acceptance_tests_config.h"

#include <cpp-subprocess/locatebinary.h>
#include <cpp-subprocess/operating_system.h>

#include <mock_operating_system.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::StrEq;

namespace ps = polysquare::subprocess;
namespace psm = polysquare::subprocess::mocks;
namespace pst = polysquare::subprocess::test;

namespace
{
    template <class T>
    T * ValueOrDefault (T *value, T *defaultValue)
    {
        if (value)
            return value;

        return defaultValue;
    }

    class TmpEnv
    {
        public:

            TmpEnv (char const *env, char const *value);
            ~TmpEnv ();

        private:

            TmpEnv (TmpEnv const &) = delete;
            TmpEnv (TmpEnv &&) = delete;
            TmpEnv & operator= (TmpEnv const &) = delete;

            std::string const mEnvironmentKey;
            std::string const mPreviousEnvironmentValue;
    };

    /* Older versions of cppcheck get confused by
     * standalone strings as part of array initializers
     * so we use an array-assign here */
    std::array <std::string, 2> MockExecutablePaths =
    {
        {
            "/mock/to/path",
            "/path/to/mock"
        }
    };

    char const *MockExecutable = "mock_executable";

    template <typename T>
    std::array <T, 1>
    SingleArray (T const &t)
    {
        return std::array <T, 1> {
                                     {
                                         t
                                     }
                                 };
    }
    
    typedef char const * CharCP;
}

TmpEnv::TmpEnv (char const *env, char const *value) :
    mEnvironmentKey (env),
mPreviousEnvironmentValue (ValueOrDefault(const_cast <CharCP> (getenv (env)),
                                          ""))
{
    setenv (env, value, 1);
}

TmpEnv::~TmpEnv ()
{
    if (mPreviousEnvironmentValue.empty ())
        unsetenv (mEnvironmentKey.c_str ());
    else
        setenv (mEnvironmentKey.c_str (),
                mPreviousEnvironmentValue.c_str (),
                1);
}

class LocateBinary :
    public ::testing::Test
{
    public:

        LocateBinary ();

    protected:

        psm::OperatingSystem os;
};

LocateBinary::LocateBinary ()
{
    os.IgnoreAllCalls ();
}

ACTION_P (SimulateError, err)
{
    errno = err;
    return -1;
}

TEST_F (LocateBinary, ThrowRuntimeErrorOnEmptyPath)
{
    std::stringstream PassedPathBuilder;
    PassedPathBuilder << "/" << MockExecutable;
    ON_CALL (os, access (StrEq (PassedPathBuilder.str ()), _))
        .WillByDefault (SimulateError (ENOENT));

    EXPECT_THROW ({
        ps::locateBinary (MockExecutable,
                          SingleArray <char const *> (""),
                          os);
    }, std::runtime_error);
}

TEST_F (LocateBinary, ThrowRuntimeErrorOnNotFoundInOnePath)
{
    std::string const PassedPath (MockExecutablePaths[0] + "/" + MockExecutable);
    ON_CALL (os, access (StrEq (PassedPath), _))
        .WillByDefault (SimulateError (ENOENT));

    EXPECT_THROW ({
        ps::locateBinary (MockExecutable,
                          SingleArray (MockExecutablePaths[0]),
                          os);
    }, std::runtime_error);
}

TEST_F (LocateBinary, ThrowRuntimeErrorOnNotFoundInMultiplePaths)
{
    std::string const PassedPath0 (MockExecutablePaths[0] + "/" + MockExecutable);
    std::string const PassedPath1 (MockExecutablePaths[1] + "/" + MockExecutable);

    ON_CALL (os, access (StrEq (PassedPath0), _))
        .WillByDefault (SimulateError (ENOENT));
    ON_CALL (os, access (StrEq (PassedPath1), _))
        .WillByDefault (SimulateError (ENOENT));

    EXPECT_THROW ({
        ps::locateBinary (MockExecutable,
                          MockExecutablePaths,
                          os);
    }, std::runtime_error);
}

TEST_F (LocateBinary, ReturnFullyQualifiedPathIfFoundInOnePath)
{
    std::string const ExpectedPath (MockExecutablePaths[0] +
                                    "/" +
                                    MockExecutable);

    ON_CALL (os, access (StrEq (ExpectedPath), _))
        .WillByDefault (Return (0));

    EXPECT_EQ (ExpectedPath, ps::locateBinary (MockExecutable,
                                               MockExecutablePaths,
                                               os));
}

TEST_F (LocateBinary, ReturnFullyQualifiedPathIfFoundOtherPath)
{
    std::string const NoFindPath (MockExecutablePaths[0] +
                                  "/" +
                                  MockExecutable);
    std::string const ExpectedPath (MockExecutablePaths[1] +
                                    "/" +
                                    MockExecutable);

    ON_CALL (os, access (StrEq (NoFindPath), _))
        .WillByDefault (SimulateError (ENOENT));
    ON_CALL (os, access (StrEq (ExpectedPath), _))
        .WillByDefault (Return (0));

    /* We really should be checking twice */
    EXPECT_CALL (os, access (_, X_OK)).Times (2);

    EXPECT_EQ (ExpectedPath,
               ps::locateBinary (MockExecutable,
                                 MockExecutablePaths,
                                 os));
}

namespace
{
    std::string const NewPath ("/:" +
                               std::string (pst::SimpleExecutableDirectory) +
                               ":" +
                               getenv ("PATH"));
    std::string const NoExecutable ("______none______");
}

class LocateBinaryWithSystemPath :
    public ::testing::Test
{
    public:

        LocateBinaryWithSystemPath ();

    protected:

        ps::OperatingSystem::Unique os;

    private:

        TmpEnv path;
};

LocateBinaryWithSystemPath::LocateBinaryWithSystemPath () :
    os (ps::MakeOperatingSystem ()),
    path ("PATH", NewPath.c_str ())
{
}

TEST_F (LocateBinaryWithSystemPath, SimpleExecutable)
{
    std::string pathString (getenv ("PATH"));
    boost::char_separator <char> sep (":");
    boost::tokenizer <boost::char_separator <char> > paths (pathString, sep);

    EXPECT_THAT (ps::locateBinary (pst::SimpleExecutable,
                                   paths,
                                   *os),
                 StrEq (pst::SimpleExecutablePath));
}
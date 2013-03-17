#ifndef MYSQL_HPP_
#define MYSQL_HPP_

#include <cassert>
#include <cstdint>
#include <cstring>
#include <mysql/mysql.h>

#include <boost/lexical_cast.hpp>
#include <string>
#include <tuple>
#include <typeinfo>
#include <utility>
#include <vector>

#include "InputBinder.hpp"
#include "OutputBinder.hpp"
#include "MySqlException.hpp"

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 6)
#define nullptr 0
#endif


class MySql
{
public:
    MySql(
        const char* const hostname,
        const char* const username,
        const char* const password,
        const char* const database,
        const uint16_t port = 3306
    );

    // Delegating constructors are supported in GCC 4.7
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
    MySql(
        const char* hostname,
        const char* username,
        const char* password,
        const uint16_t port = 3306
    );
#endif
    ~MySql();

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4)
    // Deleted constructors are supported in GCC 4.4
    MySql(const MySql& rhs) = delete;
    MySql& operator=(const MySql& rhs) = delete;
#endif

    /**
     * Normal query. Results are stored in the given vector.
     * @param query The query to run.
     * @param results A vector of tuples to store the results in.
     * @param args Arguments to bind to the query.
     */
    template<typename... InputArgs, typename... OutputArgs>
    void runQuery(
        std::vector<std::tuple<OutputArgs...>>* const results,
        const char* const query,
        InputArgs... args
    );

    /**
     * Command that doesn't return results, like "USE yelp" or
     * "INSERT INTO user VALUES ('Brandon', 28)".
     * @param query The query to run.
     * @param args Arguments to bind to the query.
     * @return The number of affected rows.
     */
    template<typename... Args>
    my_ulonglong runCommand(
        const char* const command,
        Args... args
    );
    my_ulonglong runCommand(const char* const command);

private:
    template<std::size_t I> struct int_{};  // Compile-time counter

    template<typename Tuple, size_t I>
    static void setTupleElements(
        Tuple* const returnValue,
        const std::vector<MYSQL_BIND>& bindParameters,
        int_<I>
    );
    template<typename Tuple>
    static void setTupleElements(
        Tuple* const returnValue,
        const std::vector<MYSQL_BIND>& bindParameters,
        int_<-1>
    );

    template<typename... Args>
    static void setTuple(
        std::tuple<Args...>* const t,
        const std::vector<MYSQL_BIND>& bindParameters
    );

    MYSQL* connection_;

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 4)
    // Hidden methods
    MySql(const MySql& rhs);
    MySql& operator=(const MySql& rhs);
#endif
};


template<typename... Args>
my_ulonglong MySql::runCommand(
    const char* const command,
    Args... args
)
{
    MYSQL_STMT* const statement = mysql_stmt_init(connection_);
    if (nullptr == statement)
    {
        throw MySqlException(connection_);
    }

    const size_t length = ::strlen(command);
    const int status = mysql_stmt_prepare(statement, command, length);
    if (0 != status)
    {
        MySqlException mse(statement);
        mysql_stmt_close(statement);
        throw mse;
    }

    const size_t parameterCount = mysql_stmt_param_count(statement);
    if (sizeof...(args) != parameterCount)
    {
        mysql_stmt_close(statement);
        std::string errorMessage("Incorrect number of parameters; query required ");
        errorMessage += boost::lexical_cast<std::string>(parameterCount);
        errorMessage += " but ";
        errorMessage += boost::lexical_cast<std::string>(sizeof...(args));
        errorMessage += " parameters were provided.";
        throw MySqlException(errorMessage);
    }

    std::vector<MYSQL_BIND> bindParameters;
    bindParameters.resize(parameterCount);
    InputBinder<0, Args...> binder;
    binder.bind(&bindParameters, args...);
    if (0 != mysql_stmt_bind_param(statement, &bindParameters[0]))
    {
        MySqlException mse(statement);
        mysql_stmt_close(statement);
        throw mse;
    }

    if (0 != mysql_stmt_execute(statement))
    {
        MySqlException mse(statement);
        mysql_stmt_close(statement);
        throw mse;
    }

    // If the user ran a SELECT statement or something else, at least warn them
    const my_ulonglong affectedRows = mysql_stmt_affected_rows(statement);
    if (((my_ulonglong)(-1)) == affectedRows)
    {
        MySqlException mse(statement);
        mysql_stmt_close(statement);
        throw mse;
    }

    mysql_stmt_close(statement);
    return affectedRows;
}


template<typename... InputArgs, typename... OutputArgs>
void MySql::runQuery(
    std::vector<std::tuple<OutputArgs...>>* const results,
    const char* const query,
    InputArgs... args
)
{
    assert(nullptr != results);
    MYSQL_STMT* const statement = mysql_stmt_init(connection_);

    const size_t length = ::strlen(query);
    if (0 != mysql_stmt_prepare(statement, query, length))
    {
        throw MySqlException(connection_);
    }

    // Bind the input parameters
    // Check that the parameter count is right
    const size_t parameterCount = mysql_stmt_param_count(statement);
    if (sizeof...(InputArgs) != parameterCount)
    {
        mysql_stmt_close(statement);

        std::string errorMessage(
            "Incorrect number of parameters; query required "
        );
        errorMessage += boost::lexical_cast<std::string>(parameterCount);
        errorMessage += " but ";
        errorMessage += boost::lexical_cast<std::string>(sizeof...(args));
        errorMessage += " parameters were provided.";
        throw MySqlException(errorMessage);
    }

    std::vector<MYSQL_BIND> inputBindParameters;
    inputBindParameters.resize(parameterCount);
    InputBinder<0, InputArgs...> inputBinder;
    inputBinder.bind(&inputBindParameters, args...);
    if (0 != mysql_stmt_bind_param(statement, &inputBindParameters[0]))
    {
        MySqlException mse(statement);
        mysql_stmt_close(statement);
        throw mse;
    }

    // Bind the output parameters
    // Check that the sizes match
    const size_t fieldCount = mysql_stmt_field_count(statement);
    if (fieldCount != sizeof...(OutputArgs))
    {
        MySqlException mse(connection_);
        mysql_stmt_close(statement);
        throw mse;
    }
    OutputBinder<OutputArgs...> outputBinder(statement);
    outputBinder.setResults(results);

    mysql_stmt_close(statement);
}


template<typename Tuple, size_t I>
void MySql::setTupleElements(
    Tuple* const tuple,
    const std::vector<MYSQL_BIND>& bindParameters,
    int_<I>
)
{
    assert(nullptr != tuple);

    std::get<I>(*tuple) = *static_cast<const decltype(std::get<I>(*tuple))*>(
        bindParameters.at(I).buffer
    );

    // Recursively set the rest of the elements
    setTupleElements(tuple, bindParameters, int_<I - 1>());
}


template<typename Tuple>
void MySql::setTupleElements(
    Tuple* const,
    const std::vector<MYSQL_BIND>&,
    int_<-1>
)
{
}


template<typename... Args>
void MySql::setTuple(
    std::tuple<Args...>* const t,
    const std::vector<MYSQL_BIND>& bindParameters
)
{
    setTupleElements(t, bindParameters, int_<sizeof...(Args) - 1>());
}


#endif  // MYSQL_HPP_

#include "MySql.hpp"

#include <cassert>
#include <cstdint>
#include <mysql/mysql.h>

#include <string>
#include <sstream>
#include <vector>

#include "MySqlException.hpp"


using std::string;
using std::vector;


    // Delegating constructors are supported in GCC 4.7
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)
MySql::MySql(
    const char* hostname,
    const char* username,
    const char* password,
    const uint16_t port
)
    : MySql(hostname, username, password, nullptr, port)
{
}
#endif


MySql::MySql(
    const char* const hostname,
    const char* const username,
    const char* const password,
    const char* const database,
    const uint16_t port
)
    : connection_(mysql_init(nullptr))
    , statement_(nullptr)
    , bindParameters_()
{
    if (nullptr == connection_)
    {
        throw MySqlException("Unable to connect to MySQL");
    }

    const MYSQL* const success = mysql_real_connect(
        connection_,
        hostname,
        username,
        password,
        database,
        port,
        nullptr,
        0
    );
    if (nullptr == success)
    {
        MySqlException mse(connection_);
        mysql_close(connection_);
        throw mse;
    }

    statement_ = mysql_stmt_init(connection_);
}


MySql::~MySql()
{
    mysql_close(connection_);
}


my_ulonglong MySql::runCommand(const char* const command)
{
    mysql_real_query(connection_, command, strlen(command));

    // If the user ran a SELECT statement or something else, at least warn them
    const my_ulonglong affectedRows = mysql_affected_rows(connection_);
    if ((my_ulonglong) - 1 == affectedRows)
    {
        throw MySqlException(connection_);
    }

    return affectedRows;
}


void MySql::extractRow(
    MYSQL_ROW,
    const size_t currentField,
    const size_t totalFields
) const
{
    if (currentField != totalFields)
    {
        throw MySqlException("Too many columns returned by query");
    }
}

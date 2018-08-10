#include "benchmark.h"

#include <ozo/connection_info.h>
#include <ozo/connection_pool.h>
#include <ozo/request.h>
#include <ozo/query_builder.h>

#include <boost/asio/io_service.hpp>
#include <boost/asio/spawn.hpp>

namespace {

namespace asio = boost::asio;

using benchmark_t = ozo::benchmark::time_limit_benchmark;

template <class Query>
void reuse_connection_info(const std::string& conn_string, Query query) {
    std::cout << '\n' << __func__ << '\n';

    benchmark_t benchmark;
    asio::io_context io(1);
    ozo::connection_info<> connection_info(conn_string);

    asio::spawn(io, [&] (auto yield) {
        while (true) {
            ozo::result result;
            auto connection = ozo::get_connection(ozo::make_connector(connection_info, io), yield);
            ozo::request(connection, query, result, yield);
            if (!benchmark.step(result.size())) {
                break;
            }
        }
    });

    io.run();
}

template <class Result, class Query>
void reuse_connection_info_and_parse_result(const std::string& conn_string, Query query) {
    std::cout << '\n' << __func__ << '\n';

    benchmark_t benchmark;
    asio::io_context io(1);
    ozo::connection_info<> connection_info(conn_string);

    asio::spawn(io, [&] (auto yield) {
        while (true) {
            std::vector<Result> result;
            auto connection = ozo::get_connection(connection_info, io, yield);
            ozo::request(connection, query, std::back_inserter(result), yield);
            if (!benchmark.step(result.size())) {
                break;
            }
        }
    });

    io.run();
}

template <class Query>
void reuse_connection(const std::string& conn_string, Query query) {
    std::cout << '\n' << __func__ << '\n';

    benchmark_t benchmark;
    asio::io_context io(1);
    ozo::connection_info<> connection_info(conn_string);

    asio::spawn(io, [&] (auto yield) {
        auto connection = ozo::get_connection(ozo::make_connector(connection_info, io), yield);
        while (true) {
            ozo::result result;
            ozo::request(connection, query, result, yield);
            if (!benchmark.step(result.size())) {
                break;
            }
        }
    });

    io.run();
}

template <class Result, class Query>
void reuse_connection_and_parse_result(const std::string& conn_string, Query query) {
    std::cout << '\n' << __func__ << '\n';

    benchmark_t benchmark;
    asio::io_context io(1);
    ozo::connection_info<> connection_info(conn_string);

    asio::spawn(io, [&] (auto yield) {
        auto connection = ozo::get_connection(connection_info, yield);
        while (true) {
            std::vector<Result> result;
            ozo::request(connection, query, std::back_inserter(result), yield);
            if (!benchmark.step(result.size())) {
                break;
            }
        }
    });

    io.run();
}

template <class Query>
void use_connection_pool(const std::string& conn_string, Query query) {
    std::cout << '\n' << __func__ << '\n';

    benchmark_t benchmark;
    asio::io_context io(1);
    const ozo::connection_info<> connection_info(conn_string);
    ozo::connection_pool_config config;
    config.capacity = 2;
    config.queue_capacity = 0;
    auto pool = ozo::make_connection_pool(connection_info, config);
    auto provider = ozo::make_connector(pool, io);

    asio::spawn(io, [&] (auto yield) {
        while (true) {
            ozo::result result;
            auto connection = ozo::get_connection(provider, yield);
            ozo::request(connection, query, result, yield);
            if (!benchmark.step(result.size())) {
                break;
            }
        }
    });

    io.run();
}

template <class Result, class Query>
void use_connection_pool_and_parse_result(const std::string& conn_string, Query query) {
    std::cout << '\n' << __func__ << '\n';

    benchmark_t benchmark;
    asio::io_context io(1);
    const ozo::connection_info<> connection_info(conn_string);
    ozo::connection_pool_config config;
    config.capacity = 2;
    config.queue_capacity = 0;
    auto pool = ozo::make_connection_pool(connection_info, config);
    auto provider = ozo::make_connector(pool, io);

    asio::spawn(io, [&] (auto yield) {
        while (true) {
            std::vector<Result> result;
            auto connection = ozo::get_connection(provider, yield);
            ozo::request(connection, query, std::back_inserter(result), yield);
            if (!benchmark.step(result.size())) {
                break;
            }
        }
    });

    io.run();
}

template <class Query>
void use_connection_pool_mult_connection(const std::string& conn_string, Query query, std::size_t n) {
    std::cout << '\n' << __func__ << ' ' << n << '\n';

    benchmark_t benchmark;
    asio::io_context io(1);
    const ozo::connection_info<> connection_info(conn_string);
    ozo::connection_pool_config config;
    config.capacity = n + 1;
    config.queue_capacity = 0;
    auto pool = ozo::make_connection_pool(connection_info, config);
    auto provider = ozo::make_connector(pool, io);

    for (std::size_t i = 0; i < n; ++i) {
        asio::spawn(io, [&, i] (auto yield) {
            while (true) {
                ozo::result result;
                auto connection = ozo::get_connection(provider, yield);
                ozo::request(connection, query, result, yield);
                if (!benchmark.step(result.size(), i)) {
                    break;
                }
            }
        });
    }

    io.run();
}

template <class Result, class Query>
void use_connection_pool_and_parse_result_mult_connection(const std::string& conn_string, Query query, std::size_t n) {
    std::cout << '\n' << __func__ << '\n';

    benchmark_t benchmark;
    asio::io_context io(1);
    const ozo::connection_info<> connection_info(conn_string);
    ozo::connection_pool_config config;
    config.capacity = n + 1;
    config.queue_capacity = 0;
    auto pool = ozo::make_connection_pool(connection_info, config);
    auto provider = ozo::make_connector(pool, io);

    for (std::size_t i = 0; i < n; ++i) {
        asio::spawn(io, [&, i] (auto yield) {
            while (true) {
                std::vector<Result> result;
                auto connection = ozo::get_connection(provider, yield);
                ozo::request(connection, query, std::back_inserter(result), yield);
                if (!benchmark.step(result.size(), i)) {
                    break;
                }
            }
        });
    }

    io.run();
}

} // namespace

int main(int argc, char **argv) {
    using namespace ozo::benchmark;
    using namespace ozo::literals;
    using namespace hana::literals;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <conninfo>\n";
        return 1;
    }

    const std::string conn_string(argv[1]);

    const auto simple_query = "SELECT 1"_SQL.build();

    std::cout << "\nquery: " << ozo::to_const_char(ozo::get_text(simple_query)) << '\n';
    reuse_connection_info(conn_string, simple_query);
    reuse_connection(conn_string, simple_query);
    use_connection_pool(conn_string, simple_query);
    use_connection_pool_and_parse_result<std::tuple<std::int32_t>>(conn_string, simple_query);

    const auto complex_query = (
        "SELECT typname, typnamespace, typowner, typlen, typbyval, typcategory, "_SQL +
        "typispreferred, typisdefined, typdelim, typrelid, typelem, typarray "_SQL +
        "FROM pg_type WHERE typtypmod = "_SQL + -1 + " AND typisdefined = "_SQL + true
    ).build();

    std::cout << "\nquery: " << ozo::to_const_char(ozo::get_text(complex_query)) << '\n';
    use_connection_pool(conn_string, complex_query);
    use_connection_pool_and_parse_result<pg_type>(conn_string, complex_query);
    use_connection_pool_mult_connection(conn_string, complex_query, 2);
    use_connection_pool_mult_connection(conn_string, complex_query, 4);
    use_connection_pool_mult_connection(conn_string, complex_query, 8);
    use_connection_pool_mult_connection(conn_string, complex_query, 16);
    use_connection_pool_mult_connection(conn_string, complex_query, 32);
    use_connection_pool_mult_connection(conn_string, complex_query, 64);
    use_connection_pool_and_parse_result_mult_connection<pg_type>(conn_string, complex_query, 2);
    use_connection_pool_and_parse_result_mult_connection<pg_type>(conn_string, complex_query, 4);
    use_connection_pool_and_parse_result_mult_connection<pg_type>(conn_string, complex_query, 8);

    return 0;
}

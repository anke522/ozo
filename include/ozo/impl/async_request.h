#pragma once

#include <ozo/connection.h>
#include <ozo/binary_query.h>
#include <ozo/query_builder.h>
#include <ozo/binary_deserialization.h>
#include <ozo/impl/io.h>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/coroutine.hpp>

namespace ozo {
namespace impl {

template <typename Connection, typename Handler>
struct request_operation_context {
    std::decay_t<Connection> conn;
    std::decay_t<Handler> handler;
    using strand_type = ozo::strand<decltype(get_io_context(conn))>;
    strand_type strand{get_io_context(conn)};
    query_state state = query_state::send_in_progress;

    request_operation_context(Connection conn, Handler handler)
      : conn(std::forward<Connection>(conn)),
        handler(std::forward<Handler>(handler)) {}
};

template <typename Connection, typename Handler>
inline decltype(auto) make_request_operation_context(Connection&& conn, Handler&& h) {
    return std::make_shared<request_operation_context<Connection, Handler>>(
        std::forward<Connection>(conn), std::forward<Handler>(h)
    );
}

template <typename ...Ts>
using request_operation_context_ptr = std::shared_ptr<request_operation_context<Ts...>>;

template <typename ...Ts>
inline auto& get_connection(const request_operation_context_ptr<Ts...>& ctx) noexcept {
    return ctx->conn;
}

template <typename ...Ts>
inline decltype(auto) get_handler_context(const request_operation_context_ptr<Ts...>& ctx) noexcept {
    return std::addressof(ctx->handler);
}

template <typename ...Ts>
inline query_state get_query_state(const request_operation_context_ptr<Ts...>& ctx) noexcept {
    return ctx->state;
}

template <typename ...Ts>
inline void set_query_state(const request_operation_context_ptr<Ts...>& ctx,
        query_state state) noexcept {
    ctx->state = state;
}

template <typename ... Ts>
auto& get_executor(const request_operation_context_ptr<Ts ...>& context) noexcept {
    return context->strand;
}

template <typename ... Ts>
auto& get_query(const request_operation_context_ptr<Ts ...>& context) noexcept {
    return context->query;
}

template <typename ... Ts>
auto& get_handler(const request_operation_context_ptr<Ts ...>& context) noexcept {
    return context->handler;
}

template <typename Oper, typename ...Ts>
inline void post(const request_operation_context_ptr<Ts...>& ctx, Oper&& op) {
    post(get_connection(ctx),
        bind_executor(get_executor(ctx), std::forward<Oper>(op)));
}

template <typename ...Ts>
inline void done(const request_operation_context_ptr<Ts...>& ctx, error_code ec) {
    set_query_state(ctx, query_state::error);
    decltype(auto) conn = get_connection(ctx);
    error_code _;
    get_socket(conn).cancel(_);
    post(ctx, detail::bind(get_handler(ctx), std::move(ec), conn));
}

template <typename ...Ts>
inline void done(const request_operation_context_ptr<Ts...>& ctx) {
    post(ctx, detail::bind(get_handler(ctx), error_code{}, get_connection(ctx)));
}

template <typename Continuation, typename ...Ts>
inline void write_poll(const request_operation_context_ptr<Ts...>& ctx, Continuation&& c) {
    using asio::bind_executor;
    write_poll(get_connection(ctx), bind_executor(get_executor(ctx), std::forward<Continuation>(c)));
}

template <typename Continuation, typename ...Ts>
inline void read_poll(const request_operation_context_ptr<Ts...>& ctx, Continuation&& c) {
    using asio::bind_executor;
    read_poll(get_connection(ctx), bind_executor(get_executor(ctx), std::forward<Continuation>(c)));
}

template <typename Context, typename BinaryQuery>
struct async_send_query_params_op {
    Context ctx_;
    BinaryQuery query_;

    void perform() {
        decltype(auto) conn = get_connection(ctx_);
        if (auto ec = set_nonblocking(conn)) {
            return done(ctx_, ec);
        }
        //In the nonblocking state, calls to PQsendQuery, PQputline,
        //PQputnbytes, PQputCopyData, and PQendcopy will not block
        //but instead return an error if they need to be called again.
        while (!send_query_params(conn, query_));
        post(ctx_, *this);
    }

    void operator () (error_code ec = error_code{}, std::size_t = 0) {
        // if data has been flushed or error has been set by
        // read operation no write opertion handling is needed
        // anymore.
        if (get_query_state(ctx_) != query_state::send_in_progress) {
            return;
        }

        // In case of write operation error - finish the request
        // with error.
        if (ec) {
            return done(ctx_, ec);
        }

        // Trying to flush output one more time according to the
        // documentation
        switch (flush_output(get_connection(ctx_))) {
            case query_state::error:
                done(ctx_, error::pg_flush_failed);
                break;
            case query_state::send_in_progress:
                write_poll(ctx_, *this);
                break;
            case query_state::send_finish:
                set_query_state(ctx_, query_state::send_finish);
                break;
        }
    }

    template <typename Func>
    friend void asio_handler_invoke(Func&& f, async_send_query_params_op* ctx) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Func>(f), get_handler_context(ctx->ctx_));
    }
};

template <typename Context, typename BinaryQuery>
inline auto make_async_send_query_params_op(Context&& ctx, BinaryQuery&& q) {
    return async_send_query_params_op<std::decay_t<Context>, std::decay_t<BinaryQuery>> {
        std::forward<Context>(ctx), std::forward<BinaryQuery>(q)
    };
}

template <typename T, typename ...Ts>
inline decltype(auto) make_binary_query(const query_builder<Ts...>& builder, const oid_map_t<T>& m) {
    return make_binary_query(builder.build(), m);
}

template <typename Context, typename Query>
void async_send_query_params(std::shared_ptr<Context> ctx, Query&& query) {
    auto q = make_binary_query(std::forward<Query>(query),
                        get_oid_map(get_connection(ctx)));

    make_async_send_query_params_op(std::move(ctx), std::move(q)).perform();
}

#include <boost/asio/yield.hpp>

template <typename Context, typename ResultProcessor>
struct async_get_result_op : boost::asio::coroutine {
    Context ctx_;
    ResultProcessor process_;

    async_get_result_op(Context ctx, ResultProcessor process)
    : ctx_(ctx), process_(process) {}

    void perform() {
        post(ctx_, *this);
    }

    void operator() (error_code ec = error_code{}, std::size_t = 0) {
        // In case when query error state has been set by send query params
        // operation skip handle and do nothing more.
        if (get_query_state(ctx_) == query_state::error) {
            return;
        }

        if (ec) {
            // Bad descriptor error can occur here if the connection
            // has been closed by user during processing.
            if (ec == asio::error::bad_descriptor) {
                ec = asio::error::operation_aborted;
            }
            return done(ctx_, ec);
        }

        reenter(*this) {
            while (is_busy(get_connection(ctx_))) {
                yield read_poll(ctx_, *this);
                if (auto err = consume_input(get_connection(ctx_))) {
                    return done(ctx_, err);
                }
            }

            if (auto res = get_result(get_connection(ctx_))) {
                const auto status = result_status(*res);
                switch (status) {
                    case PGRES_SINGLE_TUPLE:
                        process_and_done(std::move(res));
                        return;
                    case PGRES_TUPLES_OK:
                        process_and_done(std::move(res));
                        consume_result(get_connection(ctx_));
                        return;
                    case PGRES_COMMAND_OK:
                        done(ctx_);
                        consume_result(get_connection(ctx_));
                        return;
                    case PGRES_BAD_RESPONSE:
                        done(ctx_, error::result_status_bad_response);
                        consume_result(get_connection(ctx_));
                        return;
                    case PGRES_EMPTY_QUERY:
                        done(ctx_, error::result_status_empty_query);
                        consume_result(get_connection(ctx_));
                        return;
                    case PGRES_FATAL_ERROR:
                        done(ctx_, result_error(*res));
                        consume_result(get_connection(ctx_));
                        return;
                    case PGRES_COPY_OUT:
                    case PGRES_COPY_IN:
                    case PGRES_COPY_BOTH:
                    case PGRES_NONFATAL_ERROR:
                        break;
                }
                set_error_context(get_connection(ctx_), get_result_status_name(status));
                done(ctx_, error::result_status_unexpected);
                consume_result(get_connection(ctx_));
            } else {
                done(ctx_);
            }
        }
    }

    template <typename Result>
    void process_and_done(Result&& res) noexcept {
        try {
            process_(std::forward<Result>(res), get_connection(ctx_));
        } catch (const std::exception& e) {
            set_error_context(get_connection(ctx_), e.what());
            return done(ctx_, error::bad_result_process);
        }
        done(ctx_);
    }

    template <typename Connection>
    void consume_result(Connection&& conn) const noexcept {
        while(get_result(conn));
    }

    template <typename Func>
    friend void asio_handler_invoke(Func&& f, async_get_result_op* ctx) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Func>(f), get_handler_context(ctx->ctx_));
    }
};

#include <boost/asio/unyield.hpp>

template <typename Context, typename ResultProcessor>
inline auto make_async_get_result_op(Context&& ctx, ResultProcessor&& process) {
    return async_get_result_op<std::decay_t<Context>, std::decay_t<ResultProcessor>>{
        std::forward<Context>(ctx),
        std::forward<ResultProcessor>(process)
    };
}

template <typename Context, typename ResultProcessor>
inline void async_get_result(Context&& ctx, ResultProcessor&& process) {
    make_async_get_result_op(
        std::forward<Context>(ctx),
        std::forward<ResultProcessor>(process)
    ).perform();
}

template <typename OutHandler, typename Query, typename Handler>
struct async_request_op {
    OutHandler out_;
    Query query_;
    Handler handler_;

    template <typename Connection>
    void operator() (error_code ec, Connection conn) {
        if (ec) {
            return handler_(ec, std::move(conn));
        }

        auto ctx = make_request_operation_context(std::move(conn), std::move(handler_));

        async_send_query_params(ctx, std::move(query_));
        async_get_result(std::move(ctx), std::move(out_));
    }

    template <typename Func>
    friend void asio_handler_invoke(Func&& f, async_request_op* ctx) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Func>(f), std::addressof(ctx->handler_));
    }
};

template <typename T>
struct async_request_out_handler {
    T out;

    template <typename Handle, typename Conn>
    void operator() (Handle&& h, Conn& conn) {
        auto res = ozo::make_result(std::forward<Handle>(h));
        ozo::recv_result(res, get_oid_map(conn), out);
    }
};

template <typename T>
auto make_async_request_out_handler(T&& out) {
    return async_request_out_handler<std::decay_t<T>> {std::forward<T>(out)};
}

template <typename Query, typename OutHandler, typename Handler>
inline auto make_async_request_op(Query&& query, OutHandler&& out, Handler&& handler) {
    using result_type = impl::async_request_op<
        std::decay_t<OutHandler>,
        std::decay_t<Query>,
        std::decay_t<Handler>
    >;
    return result_type {
        std::forward<OutHandler>(out),
        std::forward<Query>(query),
        std::forward<Handler>(handler)
    };
}

template <typename P, typename Q, typename Out, typename Handler>
inline void async_request(P&& provider, Q&& query, Out&& out, Handler&& handler) {
    static_assert(ConnectionProvider<P>, "is not a ConnectionProvider");
    static_assert(Query<Q> || QueryBuilder<Q>, "is neither Query nor QueryBuilder");
    async_get_connection(std::forward<P>(provider),
        impl::make_async_request_op(
            std::forward<Q>(query),
            make_async_request_out_handler(std::forward<Out>(out)),
            std::forward<Handler>(handler)
        )
    );
}

} // namespace impl
} // namespace ozo

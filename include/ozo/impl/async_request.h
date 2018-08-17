#pragma once

#include <ozo/connection.h>
#include <ozo/binary_query.h>
#include <ozo/query_builder.h>
#include <ozo/binary_deserialization.h>
#include <ozo/time_traits.h>
#include <ozo/impl/io.h>

namespace ozo {
namespace impl {

template <typename Query, typename OutHandler, typename Handler>
struct request_operation_context {
    Query query;
    OutHandler out;
    Handler handler;

    request_operation_context(Query query, OutHandler out, Handler handler)
            : query(std::move(query)),
              out(std::move(out)),
              handler(std::move(handler)) {}
};

template <typename ...Ts>
using request_operation_context_ptr = std::shared_ptr<request_operation_context<Ts...>>;

template <typename Query, typename OutHandler, typename Handler>
decltype(auto) make_request_operation_context(Query&& query, OutHandler&& out, Handler&& handler) {
    using context_type = request_operation_context<
        std::decay_t<Query>,
        std::decay_t<OutHandler>,
        std::decay_t<Handler>
    >;
    return std::make_shared<context_type>(
        std::forward<Query>(query),
        std::forward<OutHandler>(out),
        std::forward<Handler>(handler)
    );
}

template <typename NestedContext, typename Connection>
struct request_on_connected_operation_context {
    NestedContext nested_context;
    Connection conn;
    using strand_type = ozo::strand<decltype(get_io_context(conn))>;
    strand_type strand {get_io_context(conn)};
    query_state state = query_state::send_in_progress;

    request_on_connected_operation_context(NestedContext nested_context, Connection conn)
            : nested_context(std::move(nested_context)),
              conn(std::move(conn)) {}
};

template <typename ...Ts>
using request_on_connected_operation_context_ptr = std::shared_ptr<request_on_connected_operation_context<Ts...>>;

template <typename NestedContext, typename Connection>
decltype(auto) make_request_on_connected_operation_context(NestedContext&& nested_context, Connection&& conn) {
    using context_type = request_on_connected_operation_context<
        std::decay_t<NestedContext>,
        std::decay_t<Connection>
    >;
    return std::make_shared<context_type>(
        std::forward<NestedContext>(nested_context),
        std::forward<Connection>(conn)
    );
}

template <typename ... Ts>
auto& get_query(const request_operation_context_ptr<Ts ...>& context) noexcept {
    return context->query;
}

template <typename ...Ts>
auto& get_out_handler(const request_operation_context_ptr<Ts...>& context) noexcept {
    return context->out;
}

template <typename ... Ts>
auto& get_handler(const request_operation_context_ptr<Ts ...>& context) noexcept {
    return context->handler;
}

template <typename ...Ts>
decltype(auto) get_handler_context(const request_operation_context_ptr<Ts...>& context) noexcept {
    return std::addressof(context->handler);
}

template <typename ... Ts>
auto& get_query(const request_on_connected_operation_context_ptr<Ts ...>& context) noexcept {
    return get_query(context->nested_context);
}

template <typename ...Ts>
auto& get_out_handler(const request_on_connected_operation_context_ptr<Ts...>& context) noexcept {
    return get_out_handler(context->nested_context);
}

template <typename ... Ts>
auto& get_handler(const request_on_connected_operation_context_ptr<Ts ...>& context) noexcept {
    return get_handler(context->nested_context);
}

template <typename ...Ts>
decltype(auto) get_handler_context(const request_on_connected_operation_context_ptr<Ts...>& ctx) noexcept {
    return get_handler_context(ctx->nested_context);
}

template <typename ...Ts>
auto& get_connection(const request_on_connected_operation_context_ptr<Ts...>& context) noexcept {
    return context->conn;
}

template <typename ...Ts>
query_state get_query_state(const request_on_connected_operation_context_ptr<Ts...>& context) noexcept {
    return context->state;
}

template <typename ...Ts>
void set_query_state(const request_on_connected_operation_context_ptr<Ts...>& context, query_state state) noexcept {
    context->state = state;
}

template <typename ... Ts>
auto& get_executor(const request_on_connected_operation_context_ptr<Ts ...>& context) noexcept {
    return context->strand;
}

template <typename Context, typename Oper>
void post(const Context& ctx, Oper&& op) {
    using asio::bind_executor;
    post(get_connection(ctx), bind_executor(get_executor(ctx), std::forward<Oper>(op)));
}

template <typename Context>
void done(const Context& ctx, error_code ec) {
    set_query_state(ctx, query_state::error);
    decltype(auto) conn = get_connection(ctx);
    error_code _;
    get_socket(conn).cancel(_);
    post(ctx, detail::bind(get_handler(ctx), std::move(ec), conn));
}

template <typename Context>
void done(const Context& ctx) {
    post(ctx, detail::bind(get_handler(ctx), error_code{}, get_connection(ctx)));
}

template <typename Context, typename Continuation>
void write_poll(const Context& ctx, Continuation&& c) {
    using asio::bind_executor;
    write_poll(get_connection(ctx), bind_executor(get_executor(ctx), std::forward<Continuation>(c)));
}

template <typename Context, typename Continuation>
void read_poll(const Context& ctx, Continuation&& c) {
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
auto make_async_send_query_params_op(Context&& ctx, BinaryQuery&& q) {
    return async_send_query_params_op<std::decay_t<Context>, std::decay_t<BinaryQuery>> {
        std::forward<Context>(ctx), std::forward<BinaryQuery>(q)
    };
}

template <typename T, typename ...Ts>
decltype(auto) make_binary_query(const query_builder<Ts...>& builder, const oid_map_t<T>& m) {
    return make_binary_query(builder.build(), m);
}

template <typename Context, typename BinaryQuery>
void async_send_query_params(Context&& ctx, BinaryQuery&& binary_query) {
    make_async_send_query_params_op(
        std::forward<Context>(ctx),
        std::forward<BinaryQuery>(binary_query)
    ).perform();
}

#include <boost/asio/yield.hpp>

template <typename Context, typename ResultProcessor>
struct async_get_result_op : boost::asio::coroutine {
    Context ctx_;
    ResultProcessor process_;

    async_get_result_op(Context ctx, ResultProcessor process)
        : ctx_(std::move(ctx)), process_(std::move(process)) {}

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
auto make_async_get_result_op(Context&& ctx, ResultProcessor&& process) {
    return async_get_result_op<std::decay_t<Context>, std::decay_t<ResultProcessor>>{
        std::forward<Context>(ctx),
        std::forward<ResultProcessor>(process)
    };
}

template <typename Context, typename ResultProcessor>
void async_get_result(Context&& ctx, ResultProcessor&& process) {
    make_async_get_result_op(std::forward<Context>(ctx), std::forward<ResultProcessor>(process)).perform();
}

template <typename T>
struct async_request_out_handler {
    T out;

    template <typename Conn>
    void operator() (native_result_handle&& h, Conn& conn) {
        ozo::result res(std::move(h));
        ozo::recv_result(res, get_oid_map(conn), out);
    }
};

template <typename T>
auto make_async_request_out_handler(T&& out) {
    return async_request_out_handler<std::decay_t<T>> {std::forward<T>(out)};
}

template <typename Context>
struct async_request_op {
    Context context;

    template <typename Connection>
    void operator() (error_code ec, Connection conn) {
        if (ec) {
            return std::move(get_handler(context))(ec, std::move(conn));
        }

        const auto upper_context = make_request_on_connected_operation_context(context, std::move(conn));

        async_send_query_params(upper_context,
            make_binary_query(std::move(get_query(upper_context)), get_oid_map(get_connection(upper_context))));

        async_get_result(upper_context, get_out_handler(upper_context));
    }

    template <typename Function>
    friend void asio_handler_invoke(Function&& function, async_request_op* ctx) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Function>(function), get_handler_context(ctx->context));
    }
};

template <typename Context>
auto make_async_request_op(Context&& context) {
    return async_request_op<std::decay_t<Context>> {std::forward<Context>(context)};
}

template <typename P, typename Q, typename Out, typename Handler>
void async_request(P&& provider, Q&& query, Out&& out, Handler&& handler) {
    static_assert(ConnectionProvider<P>, "is not a ConnectionProvider");
    static_assert(Query<Q> || QueryBuilder<Q>, "is neither Query nor QueryBuilder");
    async_get_connection(
        std::forward<P>(provider),
        make_async_request_op(
            make_request_operation_context(
                std::forward<Q>(query),
                make_async_request_out_handler(std::forward<Out>(out)),
                std::forward<Handler>(handler)
            )
        )
    );
}

} // namespace impl
} // namespace ozo

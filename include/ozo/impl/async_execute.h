#pragma once

#include <ozo/impl/async_request.h>

namespace ozo {
namespace impl {

template <typename P, typename Q, typename Handler>
void async_execute(P&& provider, Q&& query, const time_traits::duration& timeout, Handler&& handler) {
    static_assert(ConnectionProvider<P>, "is not a ConnectionProvider");
    static_assert(Query<Q> || QueryBuilder<Q>, "is neither Query nor QueryBuilder");
    async_get_connection(
        std::forward<P>(provider),
        make_async_request_op(
            make_request_operation_context(
                std::forward<Q>(query),
                timeout,
                [] (auto, auto) {},
                std::forward<Handler>(handler)
            )
        )
    );
}

template <typename P, typename Q, typename Handler>
void async_execute(P&& provider, Q&& query, Handler&& handler) {
    async_execute(
        std::forward<P>(provider),
        std::forward<Q>(query),
        time_traits::duration::max(),
        std::forward<Handler>(handler)
    );
}

} // namespace impl
} // namespace ozo

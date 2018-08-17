#include <connection_mock.h>
#include <test_error.h>

#include <ozo/impl/async_request.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

namespace hana = boost::hana;

using namespace testing;
using namespace ozo::tests;

using callback_mock = callback_gmock<connection_ptr<>>;

struct fixture {
    struct context {
        connection_ptr<> conn;
        callback_handler<callback_mock> handler;
        using strand_type = ozo::strand<decltype(get_io_context(conn))>;
        strand_type strand {get_io_context(conn)};
        ozo::impl::query_state state = ozo::impl::query_state::send_in_progress;

        context(connection_ptr<> conn, callback_handler<callback_mock> handler)
                : conn(std::move(conn)), handler(std::move(handler)) {}

        friend auto& get_connection(const std::shared_ptr<context>& value) noexcept {
            return value->conn;
        }

        friend decltype(auto) get_handler_context(const std::shared_ptr<context>& value) noexcept {
            return std::addressof(value->handler);
        }

        friend ozo::impl::query_state get_query_state(const std::shared_ptr<context>& value) noexcept {
            return value->state;
        }

        friend void set_query_state(const std::shared_ptr<context>& value, ozo::impl::query_state state) noexcept {
            value->state = state;
        }

        friend auto& get_executor(const std::shared_ptr<context>& value) noexcept {
            return value->strand;
        }

        friend auto& get_handler(const std::shared_ptr<context>& value) noexcept {
            return value->handler;
        }
    };

    StrictMock<connection_gmock> connection{};
    StrictMock<callback_mock> callback{};
    StrictMock<executor_gmock> executor{};
    StrictMock<executor_gmock> strand{};
    StrictMock<strand_executor_service_gmock> strand_service{};
    StrictMock<stream_descriptor_gmock> socket{};
    io_context io{executor, strand_service};
    decltype(make_connection(connection, io, socket)) conn =
            make_connection(connection, io, socket);
    std::shared_ptr<context> ctx;

    auto make_operation_context() {
        EXPECT_CALL(strand_service, get_executor()).WillOnce(ReturnRef(strand));
        return std::make_shared<context>(conn, wrap(callback));
    }

    fixture() : ctx(make_operation_context()) {}

};

using ozo::error_code;

struct async_send_query_params_op : Test {
    fixture m;
};

TEST_F(async_send_query_params_op, should_set_non_blocking_mode_and_send_query_params_and_post_continuation_in_strand) {
    EXPECT_CALL(m.connection, set_nonblocking()).WillOnce(Return(0));
    EXPECT_CALL(m.connection, send_query_params()).WillOnce(Return(1));
    EXPECT_CALL(m.executor, post(_)).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(m.strand, dispatch(_)).WillOnce(Return());

    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{}).perform();

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::send_in_progress);
}

TEST_F(async_send_query_params_op, should_set_error_state_and_cancel_io_and_invoke_callback_with_error_if_pg_set_nonbloking_failed) {
    EXPECT_CALL(m.connection, set_nonblocking()).WillOnce(Return(-1));
    EXPECT_CALL(m.socket, cancel(_)).WillOnce(Return());
    EXPECT_CALL(m.executor, post(_)).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(m.strand, dispatch(_)).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(m.callback, context_preserved()).WillOnce(Return());
    EXPECT_CALL(m.callback, call(error_code{ozo::error::pg_set_nonblocking_failed}, _))
        .WillOnce(Return());

    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{}).perform();

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::error);
}

TEST_F(async_send_query_params_op, should_call_send_query_params_while_it_returns_error) {
    // According to the documentation
    //   In the nonblocking state, calls to PQsendQuery, PQputline,
    //   PQputnbytes, PQputCopyData, and PQendcopy will not block
    //   but instead return an error if they need to be called again.
    // PQsendQueryParams is PQsendQuery family function so it must
    // conform to the same rules.

    Sequence s;
    EXPECT_CALL(m.connection, set_nonblocking()).InSequence(s).WillOnce(Return(0));
    EXPECT_CALL(m.connection, send_query_params()).InSequence(s).WillOnce(Return(0));
    EXPECT_CALL(m.connection, send_query_params()).InSequence(s).WillOnce(Return(0));
    EXPECT_CALL(m.connection, send_query_params()).InSequence(s).WillOnce(Return(1));
    EXPECT_CALL(m.executor, post(_));

    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{}).perform();

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::send_in_progress);
}

TEST_F(async_send_query_params_op, should_exit_immediately_if_query_state_is_error_and_called_with_no_error) {
    m.ctx->state = ozo::impl::query_state::error;

    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{})();

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::error);
}

TEST_F(async_send_query_params_op, should_exit_immediately_if_query_state_is_error_and_called_with_error) {
    m.ctx->state = ozo::impl::query_state::error;

    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{})(error::error);

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::error);
}

TEST_F(async_send_query_params_op, should_exit_immediately_if_query_state_is_send_finish_and_called_with_no_error) {
    m.ctx->state = ozo::impl::query_state::send_finish;

    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{})();

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::send_finish);
}

TEST_F(async_send_query_params_op, should_exit_immediately_if_query_state_is_send_finish_and_called_with_error) {
    m.ctx->state = ozo::impl::query_state::send_finish;

    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{})(error::error);

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::send_finish);
}

TEST_F(async_send_query_params_op, should_invoke_callback_with_given_error_if_called_with_error_and_query_state_is_send_in_progress) {
    EXPECT_CALL(m.socket, cancel(_)).WillOnce(Return());
    EXPECT_CALL(m.executor, post(_)).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(m.strand, dispatch(_)).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(m.callback, context_preserved()).WillOnce(Return());
    EXPECT_CALL(m.callback, call(error_code{error::error}, _)).WillOnce(Return());

    m.ctx->state = ozo::impl::query_state::send_in_progress;
    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{})(error::error);

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::error);
}

TEST_F(async_send_query_params_op, should_exit_if_flush_output_returns_send_finish) {
    EXPECT_CALL(m.connection, flush_output())
        .WillOnce(Return(ozo::impl::query_state::send_finish));

    m.ctx->state = ozo::impl::query_state::send_in_progress;
    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{})();

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::send_finish);
}

TEST_F(async_send_query_params_op, should_invoke_callback_with_pg_flush_failed_if_flush_output_returns_error) {
    EXPECT_CALL(m.connection, flush_output())
        .WillOnce(Return(ozo::impl::query_state::error));
    EXPECT_CALL(m.socket, cancel(_)).WillOnce(Return());
    EXPECT_CALL(m.executor, post(_)).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(m.strand, dispatch(_)).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(m.callback, context_preserved()).WillOnce(Return());
    EXPECT_CALL(m.callback, call(error_code{ozo::error::pg_flush_failed}, _))
        .WillOnce(Return());

    m.ctx->state = ozo::impl::query_state::send_in_progress;
    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{})();

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::error);
}

TEST_F(async_send_query_params_op, should_wait_for_write_if_flush_output_returns_send_in_progress) {
    EXPECT_CALL(m.connection, flush_output())
        .WillOnce(Return(ozo::impl::query_state::send_in_progress));
    EXPECT_CALL(m.socket, async_write_some(_))
        .WillOnce(Return());

    m.ctx->state = ozo::impl::query_state::send_in_progress;
    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{})();

    EXPECT_EQ(m.ctx->state, ozo::impl::query_state::send_in_progress);
}

TEST_F(async_send_query_params_op, should_wait_for_write_in_strand) {
    Sequence s;
    EXPECT_CALL(m.connection, flush_output()).InSequence(s)
        .WillOnce(Return(ozo::impl::query_state::send_in_progress));
    EXPECT_CALL(m.socket, async_write_some(_)).WillOnce(InvokeArgument<0>(error_code{}));
    EXPECT_CALL(m.strand, dispatch(_)).Times(AtLeast(1)).WillRepeatedly(InvokeArgument<0>());
    EXPECT_CALL(m.callback, context_preserved()).WillOnce(Return());
    EXPECT_CALL(m.connection, flush_output()).InSequence(s)
        .WillOnce(Return(ozo::impl::query_state::send_finish));

    m.ctx->state = ozo::impl::query_state::send_in_progress;
    ozo::impl::make_async_send_query_params_op(m.ctx, fake_query{})();
}

} // namespace

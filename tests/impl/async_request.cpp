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

using ozo::impl::query_state;
using ozo::error_code;
using ozo::time_traits;

struct async_request_op : Test {
    struct request_operation_context {
        connection_ptr<> conn;
        callback_handler<callback_mock> handler;
        ozo::time_traits::duration timeout {0};
        using strand_type = ozo::strand<decltype(get_io_context(conn))>;
        strand_type strand {get_io_context(conn)};
        ozo::impl::query_state state = ozo::impl::query_state::send_in_progress;
        ozo::result result {};
        std::function<void (boost::optional<ozo::tests::pg_result>&&, connection_ptr<>&)> out_hadler {[] (auto&&, auto&) {}};
        fake_query query {};

        request_operation_context(connection_ptr<> conn, callback_handler<callback_mock> handler)
                : conn(std::move(conn)), handler(std::move(handler)) {}

        friend auto& get_query(const std::shared_ptr<request_operation_context>& value) noexcept {
            return value->query;
        }

        friend auto& get_timeout(const std::shared_ptr<request_operation_context>& value) noexcept {
            return value->timeout;
        }

        friend decltype(auto) get_handler_context(const std::shared_ptr<request_operation_context>& value) noexcept {
            return std::addressof(value->handler);
        }

        friend auto& get_out_handler(const std::shared_ptr<request_operation_context>& value) noexcept {
            return value->out_hadler;
        }

        friend auto& get_handler(const std::shared_ptr<request_operation_context>& value) noexcept {
            return value->handler;
        }
    };

    StrictMock<connection_gmock> connection {};
    StrictMock<callback_mock> callback {};
    StrictMock<executor_gmock> executor {};
    StrictMock<executor_gmock> strand {};
    StrictMock<strand_executor_service_gmock> strand_service {};
    StrictMock<stream_descriptor_gmock> socket {};
    StrictMock<steady_timer_gmock> timer {};
    io_context io {executor, strand_service, &timer};
    decltype(make_connection(connection, io, socket)) conn =
            make_connection(connection, io, socket);
    std::shared_ptr<request_operation_context> context;

    auto make_operation_context() {
        EXPECT_CALL(strand_service, get_executor()).WillOnce(ReturnRef(strand));
        return std::make_shared<request_operation_context>(conn, wrap(callback));
    }

    async_request_op() : context(make_operation_context()) {}
};

TEST_F(async_request_op, should_set_timer_and_send_query_params_and_get_result_and_call_handler) {
    context->timeout = time_traits::duration(42);

    Sequence s;

    EXPECT_CALL(strand_service, get_executor()).InSequence(s).WillOnce(ReturnRef(strand));
    EXPECT_CALL(timer, expires_after(time_traits::duration(42))).InSequence(s).WillOnce(Return(0));
    EXPECT_CALL(timer, async_wait(_)).InSequence(s).WillOnce(Return());

    // Send query params
    EXPECT_CALL(connection, set_nonblocking()).InSequence(s).WillOnce(Return(0));
    EXPECT_CALL(connection, send_query_params()).InSequence(s).WillOnce(Return(1));

    EXPECT_CALL(executor, post(_)).InSequence(s).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(strand, dispatch(_)).InSequence(s).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(callback, context_preserved()).InSequence(s).WillOnce(Return());
    EXPECT_CALL(connection, flush_output()).InSequence(s).WillOnce(Return(ozo::impl::query_state::send_finish));

    // Get result
    EXPECT_CALL(executor, post(_)).InSequence(s).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(strand, dispatch(_)).InSequence(s).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(callback, context_preserved()).InSequence(s).WillOnce(Return());
    EXPECT_CALL(connection, is_busy()).InSequence(s).WillOnce(Return(false));
    EXPECT_CALL(connection, get_result()).InSequence(s).WillOnce(Return(boost::none));
    EXPECT_CALL(timer, cancel()).InSequence(s).WillOnce(Return(1));

    EXPECT_CALL(executor, post(_)).InSequence(s).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(strand, dispatch(_)).InSequence(s).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(callback, context_preserved()).InSequence(s).WillOnce(Return());
    EXPECT_CALL(callback, call(error_code {}, _)).InSequence(s).WillOnce(Return());

    ozo::impl::make_async_request_op(context)(error_code {}, conn);
}

TEST_F(async_request_op, should_cancel_socket_on_timeout) {
    context->timeout = time_traits::duration(42);

    Sequence s;

    EXPECT_CALL(strand_service, get_executor()).InSequence(s).WillOnce(ReturnRef(strand));
    EXPECT_CALL(timer, expires_after(time_traits::duration(42))).InSequence(s).WillOnce(Return(0));
    EXPECT_CALL(timer, async_wait(_)).InSequence(s).WillOnce(InvokeArgument<0>(error_code {}));

    EXPECT_CALL(strand, dispatch(_)).InSequence(s).WillOnce(InvokeArgument<0>());
    EXPECT_CALL(socket, cancel(_)).InSequence(s).WillOnce(Return());

    // Send query params
    EXPECT_CALL(connection, set_nonblocking()).InSequence(s).WillOnce(Return(0));
    EXPECT_CALL(connection, send_query_params()).InSequence(s).WillOnce(Return(1));

    EXPECT_CALL(executor, post(_)).InSequence(s).WillOnce(Return());

    // Get result
    EXPECT_CALL(executor, post(_)).InSequence(s).WillOnce(Return());

    ozo::impl::make_async_request_op(context)(error_code {}, conn);
}

} // namespace

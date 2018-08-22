#include "test_asio.h"

#include <ozo/connection.h>

#include <boost/make_shared.hpp>
#include <boost/fusion/adapted/std_tuple.hpp>
#include <boost/fusion/adapted/struct/define_struct.hpp>
#include <boost/fusion/include/define_struct.hpp>
#include <boost/hana/adapt_adt.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

namespace hana = boost::hana;

using namespace testing;
using namespace ozo::tests;

using ozo::error_code;
using ozo::empty_oid_map;

enum class native_handle { bad, good };

inline bool connection_status_bad(const native_handle* h) {
    return *h == native_handle::bad;
}

struct io_context_mock {
    template <typename Handler>
    void post(Handler&& h) {
        asio_post(std::forward<Handler>(h));
    }

    template <typename Handler>
    void dispatch(Handler&& h) {
        asio_post(std::forward<Handler>(h));
    }
};

struct native_handle_mock {
    MOCK_METHOD1(assign, void (ozo::error_code&));
    MOCK_METHOD0(release, void ());
};

struct socket_mock {
    io_context_mock* io_;
    std::shared_ptr<native_handle_mock> native_handle_ = std::make_shared<native_handle_mock>();

    socket_mock(io_context_mock& io) : io_(std::addressof(io)) {}

    io_context_mock& get_io_service() {
        return *io_;
    }

    void assign(std::shared_ptr<native_handle_mock> handle, ozo::error_code& ec) {
        native_handle_ = std::move(handle);
        native_handle_->assign(ec);
    }

    std::shared_ptr<native_handle_mock> native_handle() const {
        return native_handle_;
    }

    void release() {
        native_handle_->release();
        native_handle_.reset();
    }
};

template <typename OidMap = empty_oid_map>
struct connection {
    using handle_type = std::unique_ptr<native_handle>;

    handle_type handle_ = std::make_unique<native_handle>();
    socket_mock socket_;
    OidMap oid_map_;
    std::string error_context_;

    explicit connection(io_context_mock& io) : socket_(io) {}

};

template <typename ...Ts>
using connection_ptr = std::shared_ptr<connection<Ts...>>;

static_assert(ozo::Connection<connection<>>,
    "connection does not meet Connection requirements");
static_assert(ozo::Connection<connection_ptr<>>,
    "connection_ptr does not meet Connection requirements");

static_assert(!ozo::Connection<int>,
    "int meets Connection requirements unexpectedly");

struct connection_good : Test {
    io_context_mock io;
};

TEST_F(connection_good, should_return_false_for_object_with_bad_handle) {
    auto conn = std::make_shared<connection<>>(io);
    *(conn->handle_) = native_handle::bad;
    EXPECT_FALSE(ozo::connection_good(conn));
}

TEST_F(connection_good, should_return_false_for_object_with_nullptr) {
    connection_ptr<> conn;
    EXPECT_FALSE(ozo::connection_good(conn));
}

TEST_F(connection_good, should_return_true_for_object_with_good_handle) {
    auto conn = std::make_shared<connection<>>(io);
    *(conn->handle_) = native_handle::good;
    EXPECT_TRUE(ozo::connection_good(conn));
}

struct connection_bad : Test {
    io_context_mock io;
};

TEST_F(connection_bad, should_return_true_for_object_with_bad_handle) {
    auto conn = std::make_shared<connection<>>(io);
    *(conn->handle_) = native_handle::bad;
    EXPECT_TRUE(ozo::connection_bad(conn));
}

TEST_F(connection_bad, should_return_true_for_object_with_nullptr) {
    connection_ptr<> conn;
    EXPECT_TRUE(ozo::connection_bad(conn));
}

TEST_F(connection_bad, should_return_false_for_object_with_good_handle) {
    auto conn = std::make_shared<connection<>>(io);
    *(conn->handle_) = native_handle::good;
    EXPECT_FALSE(ozo::connection_bad(conn));
}

TEST(unwrap_connection, should_return_connection_reference_for_connection_wrapper) {
    io_context_mock io;
    auto conn = std::make_shared<connection<>>(io);

    EXPECT_EQ(
        std::addressof(ozo::unwrap_connection(conn)),
        conn.get()
    );
}

TEST(unwrap_connection, should_return_argument_reference_for_connection) {
    io_context_mock io;
    connection<> conn(io);

    EXPECT_EQ(
        std::addressof(ozo::unwrap_connection(conn)),
        std::addressof(conn)
    );
}

TEST(get_error_context, should_returns_reference_to_error_context) {
    io_context_mock io;
    auto conn = std::make_shared<connection<>>(io);

    EXPECT_EQ(
        std::addressof(ozo::get_error_context(conn)),
        std::addressof(conn->error_context_)
    );
}

TEST(set_error_context, should_set_error_context) {
    io_context_mock io;
    auto conn = std::make_shared<connection<>>(io);
    ozo::set_error_context(conn, "brand new super context");

    EXPECT_EQ(conn->error_context_, "brand new super context");
}

TEST(reset_error_context, should_resets_error_context) {
    io_context_mock io;
    auto conn = std::make_shared<connection<>>(io);
    conn->error_context_ = "brand new super context";
    ozo::reset_error_context(conn);
    EXPECT_TRUE(conn->error_context_.empty());
}

struct async_get_connection : Test {
    io_context_mock io;
};

TEST_F(async_get_connection, should_pass_through_the_connection_to_handler) {
    auto conn = std::make_shared<connection<>>(io);
    using callback_mock = callback_gmock<decltype(conn)>;
    StrictMock<callback_mock> cb_mock{};
    EXPECT_CALL(cb_mock, context_preserved()).WillOnce(Return());
    EXPECT_CALL(cb_mock, call(error_code{}, conn)).WillOnce(Return());
    ozo::async_get_connection(conn, wrap(cb_mock));
}

TEST_F(async_get_connection, should_reset_connection_error_context) {
    auto conn = std::make_shared<connection<>>(io);
    conn->error_context_ = "some context here";
    ozo::async_get_connection(conn, [](error_code, auto conn) {
        EXPECT_TRUE(conn->error_context_.empty());
    });
}

TEST(rebind_connection_io_context, should_leave_same_io_context_and_socket_when_address_of_new_io_is_equal_to_old) {
    io_context_mock io;
    connection<> conn(io);
    EXPECT_EQ(ozo::impl::rebind_connection_io_context(conn, io), error_code());
    EXPECT_EQ(std::addressof(ozo::get_io_context(conn)), std::addressof(io));
}

TEST(rebind_connection_io_context, should_change_socket_when_address_of_new_io_is_not_equal_to_old) {
    io_context_mock old_io;
    connection<> conn(old_io);
    io_context_mock new_io;

    EXPECT_CALL(*ozo::get_socket(conn).native_handle(), assign(_)).WillOnce(Return());
    EXPECT_CALL(*ozo::get_socket(conn).native_handle(), release()).WillOnce(Return());

    EXPECT_EQ(ozo::impl::rebind_connection_io_context(conn, new_io), error_code());
    EXPECT_EQ(std::addressof(ozo::get_io_context(conn)), std::addressof(new_io));
}

TEST(rebind_connection_io_context, should_return_error_when_socket_assign_fails_with_error) {
    io_context_mock old_io;
    connection<> conn(old_io);
    io_context_mock new_io;

    EXPECT_CALL(*ozo::get_socket(conn).native_handle(), assign(_))
        .WillOnce(SetArgReferee<0>(error_code(error::code::error)));

    EXPECT_EQ(ozo::impl::rebind_connection_io_context(conn, new_io), error_code(error::code::error));
}

struct fake_native_pq_handle {
    std::string message;
    friend const char* PQerrorMessage(const fake_native_pq_handle& self) {
        return self.message.c_str();
    }
};

TEST(connection_error_message, should_trim_trailing_soaces){
    fake_native_pq_handle handle{"error message with trailing spaces   "};
    EXPECT_EQ(std::string(ozo::impl::connection_error_message(handle)),
        "error message with trailing spaces");
}

TEST(connection_error_message, should_preserve_string_without_trailing_spaces){
    fake_native_pq_handle handle{"error message without trailing spaces"};
    EXPECT_EQ(std::string(ozo::impl::connection_error_message(handle)),
        "error message without trailing spaces");
}

TEST(connection_error_message, should_preserve_empty_string){
    fake_native_pq_handle handle{""};
    EXPECT_EQ(std::string(ozo::impl::connection_error_message(handle)),
        "");
}

TEST(connection_error_message, should_return_empty_string_for_string_of_spaces){
    fake_native_pq_handle handle{"    "};
    EXPECT_EQ(std::string(ozo::impl::connection_error_message(handle)),
        "");
}

} //namespace

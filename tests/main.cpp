#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ubip/buffer.hpp"

TEST_CASE("Construction of buffer")
{
    ubip::buffer<int, 10> buffer;
    auto reader_writer = buffer.take_reader_writer();
    CHECK(reader_writer.has_value());
    CHECK(reader_writer->reader.values().empty());

    reader_writer = buffer.take_reader_writer();
    CHECK_FALSE(reader_writer.has_value());
}

TEST_CASE("Produce consume 1")
{
    ubip::buffer<int, 10> buffer;
    auto [reader, writer] = *buffer.take_reader_writer();
    auto span = writer.prepare(1);
    REQUIRE(span.size() == 1);
    span[0] = 10;
    writer.commit(1);

    auto read_span = reader.values();
    REQUIRE(read_span.size() == 1);
    REQUIRE(read_span[0] == 10);
    reader.consume(1);
}

TEST_CASE("Wraparound")
{
    ubip::buffer<int, 10> buffer;
    auto [reader, writer] = *buffer.take_reader_writer();
    auto span = writer.prepare(10);
    auto const*const start_of_buffer = span.data();
    REQUIRE(span.size() == 10);

    int i=0;
    for(auto &b: span) {
        b = i;
        writer.commit(1);

        auto read_span = reader.values();
        REQUIRE(read_span.size() == 1);
        REQUIRE(read_span[0] == i);
        reader.consume(1);
    }

    span = writer.prepare(7);
    REQUIRE(span.size() == 7);
    REQUIRE(span.data() == start_of_buffer);
    writer.commit(7);
    reader.consume(7);
    span = writer.prepare(5);
    REQUIRE(span.size() == 5);
    REQUIRE(span.data() == start_of_buffer);
    writer.commit(5);
    auto read_span = reader.values();
    REQUIRE(read_span.data() == start_of_buffer);
    REQUIRE(read_span.size() == 5);

    span = writer.prepare(2);
    REQUIRE(span.data() == start_of_buffer+5);
    REQUIRE(span.size() == 2);
}
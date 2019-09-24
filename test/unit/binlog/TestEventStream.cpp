#include <binlog/EventStream.hpp>

#include <binlog/Entries.hpp>

#include <mserialize/make_template_serializable.hpp>
#include <mserialize/serialize.hpp>
#include <mserialize/visit.hpp>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>

namespace {

template <typename Entry>
void serializeSizePrefixed(const Entry& entry, std::ostream& out)
{
  const std::uint32_t size = std::uint32_t(mserialize::serialized_size(entry));
  mserialize::serialize(size, out);
  mserialize::serialize(entry, out);
}

template <typename Entry>
void serializeSizePrefixedTagged(const Entry& entry, std::ostream& out)
{
  const auto tag = Entry::Tag;
  const std::uint32_t size = std::uint32_t(mserialize::serialized_size(entry) + sizeof(tag));
  mserialize::serialize(size, out);
  mserialize::serialize(tag, out);
  mserialize::serialize(entry, out);
}

template <typename... Args>
struct TestEvent
{
  std::uint64_t eventSourceId;
  std::tuple<Args...> args;
};

binlog::EventSource testEventSource(std::uint64_t id, const std::string& seed = "foo", std::string argumentTags = {})
{
  return binlog::EventSource{
    id, binlog::Severity::info, seed, seed, seed, seed.size(), seed, std::move(argumentTags)
  };
}

class ArgumentsToString
{
  std::stringstream _str;

public:
  ArgumentsToString() { _str << std::boolalpha; }

  // catch all for arithmetic types
  template <typename T>
  void visit(T v) { _str << v << ' '; }

  // avoid displaying int8_t and uint8_t as a character
  void visit(std::int8_t v)    { _str << int(v) << ' '; }
  void visit(std::uint8_t v)   { _str << unsigned(v) << ' '; }

  void visit(mserialize::Visitor::SequenceBegin)  { _str << "[ "; }
  void visit(mserialize::Visitor::SequenceEnd)    { _str << "] "; }

  void visit(mserialize::Visitor::TupleBegin)     { _str << "( "; }
  void visit(mserialize::Visitor::TupleEnd)       { _str << ") "; }

  void visit(mserialize::Visitor::VariantBegin)   {}
  void visit(mserialize::Visitor::VariantEnd)     {}
  void visit(mserialize::Visitor::Null)           { _str << "{null} "; }

  void visit(mserialize::Visitor::Enum e)         { _str << e.enumerator << " "; }

  void visit(mserialize::Visitor::StructBegin sb) { _str << sb.name << "{ "; }
  void visit(mserialize::Visitor::StructEnd)      { _str << "} "; }

  void visit(mserialize::Visitor::FieldBegin fb)  { _str << fb.name << ": "; }
  void visit(mserialize::Visitor::FieldEnd)       {}

  std::string value() const { return _str.str(); }
};

} // namespace

namespace binlog {

bool operator==(const EventSource& a, const EventSource& b)
{
  return a.id == b.id
    &&   a.severity == b.severity
    &&   a.category == b.category
    &&   a.function == b.function
    &&   a.file == b.file
    &&   a.line == b.line
    &&   a.formatString == b.formatString
    &&   a.argumentTags == b.argumentTags;
}

std::ostream& operator<<(std::ostream& out, const EventSource& a)
{
  return
  out << "EventSource{"
      << " id: " << a.id
      << " severity: " << static_cast<std::uint16_t>(a.severity)
      << " category: " << a.category
      << " function: " << a.function
      << " file: " << a.file
      << " line: " << a.line
      << " formatString: " << a.formatString
      << " argumentTags: " << a.argumentTags << " }";
}

} // namespace binlog

MSERIALIZE_MAKE_TEMPLATE_SERIALIZABLE(
  (typename... Args), (TestEvent<Args...>), eventSourceId, args
)

BOOST_AUTO_TEST_SUITE(EventStream)

BOOST_AUTO_TEST_CASE(read_event)
{
  const binlog::EventSource eventSource = testEventSource(123);
  const TestEvent<> event{123, {}};

  std::stringstream stream;
  serializeSizePrefixedTagged(eventSource, stream);
  serializeSizePrefixed(event, stream);

  binlog::EventStream eventStream(stream);

  const binlog::Event* e1 = eventStream.nextEvent();
  BOOST_TEST_REQUIRE(e1 != nullptr);
  BOOST_TEST_REQUIRE(e1->source != nullptr);
  BOOST_TEST(*e1->source == eventSource);
  BOOST_TEST(e1->arguments.empty());

  const binlog::Event* e2 = eventStream.nextEvent();
  BOOST_TEST(e2 == nullptr);
}

BOOST_AUTO_TEST_CASE(read_event_with_args)
{
  const binlog::EventSource eventSource = testEventSource(123, "foobar", "(iy[c)");
  const TestEvent<int, bool, std::string> event{123, {789, true, "foo"}};

  std::stringstream stream;
  serializeSizePrefixedTagged(eventSource, stream);
  serializeSizePrefixed(event, stream);

  binlog::EventStream eventStream(stream);

  const binlog::Event* e1 = eventStream.nextEvent();
  BOOST_TEST_REQUIRE(e1 != nullptr);
  BOOST_TEST_REQUIRE(e1->source != nullptr);
  BOOST_TEST(*e1->source == eventSource);

  ArgumentsToString visitor;
  binlog::Range arguments(e1->arguments);
  mserialize::visit(e1->source->argumentTags, visitor, arguments);
  BOOST_TEST(visitor.value() == "( 789 true [ f o o ] ) ");

  const binlog::Event* e2 = eventStream.nextEvent();
  BOOST_TEST(e2 == nullptr);
}

BOOST_AUTO_TEST_CASE(multiple_sources)
{
  const binlog::EventSource eventSource1 = testEventSource(123, "foo");
  const binlog::EventSource eventSource2 = testEventSource(0, "bar");
  const binlog::EventSource eventSource3 = testEventSource(124, "baz");
  const TestEvent<> event1{123, {}};
  const TestEvent<> event2{124, {}};
  const TestEvent<> event3{0, {}};
  const TestEvent<> event4{123, {}};

  std::stringstream stream;
  serializeSizePrefixedTagged(eventSource1, stream);
  serializeSizePrefixedTagged(eventSource2, stream);
  serializeSizePrefixedTagged(eventSource3, stream);
  serializeSizePrefixed(event1, stream);
  serializeSizePrefixed(event2, stream);
  serializeSizePrefixed(event3, stream);
  serializeSizePrefixed(event4, stream);

  binlog::EventStream eventStream(stream);

  const std::array<const binlog::EventSource*, 4> sources{
    &eventSource1, &eventSource3, &eventSource2, &eventSource1
  };
  for (const binlog::EventSource* source : sources)
  {
    const binlog::Event* e = eventStream.nextEvent();
    BOOST_TEST_REQUIRE(e != nullptr);
    BOOST_TEST_REQUIRE(e->source != nullptr);
    BOOST_TEST(*e->source == *source);
  }
}

BOOST_AUTO_TEST_CASE(override_event_source)
{
  const binlog::EventSource eventSource1 = testEventSource(123, "foo");
  const binlog::EventSource eventSource2 = testEventSource(123, "bar");
  const TestEvent<> event{123, {}};

  std::stringstream stream;
  serializeSizePrefixedTagged(eventSource1, stream);
  serializeSizePrefixedTagged(eventSource2, stream);
  serializeSizePrefixed(event, stream);

  binlog::EventStream eventStream(stream);

  const binlog::Event* e1 = eventStream.nextEvent();
  BOOST_TEST_REQUIRE(e1 != nullptr);
  BOOST_TEST_REQUIRE(e1->source != nullptr);
  BOOST_TEST(*e1->source == eventSource2);
}

BOOST_AUTO_TEST_CASE(read_event_invalid_source)
{
  const binlog::EventSource eventSource = testEventSource(123);
  const TestEvent<> event{124, {}};

  std::stringstream stream;
  serializeSizePrefixedTagged(eventSource, stream);
  serializeSizePrefixed(event, stream);

  binlog::EventStream eventStream(stream);

  BOOST_CHECK_THROW(eventStream.nextEvent(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(continue_after_event_invalid_source)
{
  const binlog::EventSource eventSource = testEventSource(123);
  const TestEvent<> event1{124, {}};
  const TestEvent<> event2{123, {}};

  std::stringstream stream;
  serializeSizePrefixedTagged(eventSource, stream);
  serializeSizePrefixed(event1, stream);
  serializeSizePrefixed(event2, stream);

  binlog::EventStream eventStream(stream);

  BOOST_CHECK_THROW(eventStream.nextEvent(), std::runtime_error);

  const binlog::Event* e = eventStream.nextEvent();
  BOOST_TEST_REQUIRE(e != nullptr);
  BOOST_TEST_REQUIRE(e->source != nullptr);
  BOOST_TEST(*e->source == eventSource);
}

BOOST_AUTO_TEST_CASE(incomplete_size)
{
  std::stringstream stream;
  stream.write("abcd", 4);
  stream.seekg(2);

  binlog::EventStream eventStream(stream);

  BOOST_CHECK_THROW(eventStream.nextEvent(), std::runtime_error);
  BOOST_TEST(stream.tellg() == 2);
}

BOOST_AUTO_TEST_CASE(incomplete_event)
{
  std::stringstream stream;
  stream.write("abc", 3);

  const binlog::EventSource eventSource = testEventSource(123);
  serializeSizePrefixedTagged(eventSource, stream);

  // drop last byte of stream
  std::string content = stream.str();
  content.resize(content.size() - 1);
  stream.str(content);
  stream.seekg(3);

  binlog::EventStream eventStream(stream);

  BOOST_CHECK_THROW(eventStream.nextEvent(), std::runtime_error);
  BOOST_TEST(stream.tellg() == 3);
}

BOOST_AUTO_TEST_SUITE_END()
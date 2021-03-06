#ifndef BINLOG_PRETTY_PRINTER_HPP
#define BINLOG_PRETTY_PRINTER_HPP

#include <binlog/Entries.hpp>
#include <binlog/Time.hpp>

#include <cstdint>
#include <iosfwd>
#include <string>

namespace binlog {

/**
 * Convert Events to string, according to the specified format.
 *
 * Event format placeholders:
 *
 *    %I Source id
 *    %S Severity
 *    %C Category
 *    %M Function
 *    %F File, full path
 *    %G File, file name only
 *    %L Line
 *    %P Format string
 *    %T Argument tags
 *    %n Writer (thread) name
 *    %t Writer (thread) id
 *    %d Timestamp, in producer timezone
 *    %u Timestamp, in UTC
 *    %r Timestamp, raw clock value
 *    %m Message (format string with arguments substituted)
 *    %% Literal %
 *
 * Time format placeholders (used by %d and %u):
 *
 *    %Y, %y, %m, %d, %H, %M, %S, %z, %Z as for strftime
 *    %N Nanoseconds (0-999999999)
 */
class PrettyPrinter
{
public:
  explicit PrettyPrinter(
    std::string eventFormat = "%S [%d] %n %m (%G:%L)\n",
    std::string timeFormat = "%m/%d %H:%M:%S.%N"
  );

  /**
   * Print `event` using `writerProp` and `clockSync`
   * to `out`, according the the format specified in the consturctor.
   *
   * If clockSync.clockFrequency is zero,
   * broken down timestamps (%d and %u) are shown
   * as: "no_clock_sync?", as there's not enough context to
   * render them. The raw clock value remains accessible via %r.
   *
   * @pre event.source must be valid
   */
  void printEvent(
    std::ostream& out,
    const Event& event,
    const WriterProp& writerProp = {},
    const ClockSync& clockSync = {}
  ) const;

private:
  void printEventField(
    std::ostream& out,
    char spec,
    const Event& event,
    const WriterProp& writerProp,
    const ClockSync& clockSync
  ) const;

  void printEventMessage(std::ostream& out, const Event& event) const;

  void printProducerLocalTime(std::ostream& out, const ClockSync& clockSync, std::uint64_t clockValue) const;
  void printUTCTime(std::ostream& out, const ClockSync& clockSync, std::uint64_t clockValue) const;

  void printTime(std::ostream& out, BrokenDownTime& bdt, int tzoffset, const char* tzname) const;
  void printTimeField(std::ostream& out, char spec, BrokenDownTime& bdt, int tzoffset, const char* tzname) const;

  std::string _eventFormat;
  std::string _timeFormat;
};

} // namespace binlog

#endif // BINLOG_PRETTY_PRINTER_HPP

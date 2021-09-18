//
//! \file common/time.hh
//
//! Time utility routines
//
// $Id: time.hh,v 1.8 2013/06/20 08:24:12 vg Exp $
//

#ifndef COMMON_TIME_HH
# define COMMON_TIME_HH

# include <stdint.h>
# include <time.h>
# include <string>

#if defined(_WIN32)
# include <windows.h>
#endif

class DiffTime;

namespace time_internal {
  struct time_rec {
    //! The integer seconds component. Valid range in Time is all
    //! positive values; in DiffTime both negative and positive values
    //! are allowed.
    int64_t sec;
    //! The integer nanosecond component. Valid range is
    //! 0<=nsec<1E9. When representing negative time
    //! differences, for example -5.3 seconds, we will represent this
    //! as -6 seconds and 7E8 nanoseconds.
    int64_t nsec;
    time_rec(int64_t s, int64_t n ) : sec(s), nsec(n) {}
    inline bool operator==(const time_rec &b) const {
      return sec == b.sec && nsec == b.nsec;
    }
  };
}


class Time {
public:
  Time(int64_t sec, int64_t nsec);
  explicit Time(time_t t);
  Time();

# if defined(_WIN32)
  Time(const FILETIME &f);
  FILETIME toFileTime() const;
# endif

#if defined(__unix__) || defined(__APPLE__)
  struct timespec toTimespec() const;
# endif
  //
  // operators/funtions for Time
  //
  double to_double() const;
  time_t to_timet() const { return time_t(tim.sec); }

  //! Return time-stamp for 'now'
  static Time now();

  //
  // Aritmetic operators
  //
  const DiffTime operator-(const Time& p1 ) const;
  const Time operator-(const DiffTime& p2 ) const;
  const Time operator+(const DiffTime& p2 ) const;
  Time& operator+=(const DiffTime& p2 );
  Time& operator-=(const DiffTime& p2 );
  //
  // Time compare operators
  //
  bool operator<(const Time& p2) const;
  bool operator>(const Time& p2) const;
  bool operator<=(const Time& p2) const;
  bool operator>=(const Time& p2) const;
  bool operator==(const Time& p2) const;
  bool operator!=(const Time& p2) const;

  //
  // Constants
  //
  static const Time BEGINNING_OF_TIME;
  static const Time END_OF_TIME;
protected:
  //! Our time record
  time_internal::time_rec tim;

  // t must represent UTC time
  Time(struct tm t);
};


//!
//! Parsing of ISO 8601 timestamps
//
//! We do not support parsing local-time time stamps, as "local time"
//! would be prone to ambiguity (we often send stuff over a network,
//! so local time is not just local time on the system we run on)
//
//! We support the ISO 8601 extended format for combined date and
//! time. Examples follow:
//
//! <YYYY-MM-DD>{T, }<hh:mm:ss>[{,,.}<fractional seconds>]{Z,+00}
//
//! 2013-02-05T20:41:46.123Z   - 2012 February 5th, 20:41:46 UTC
//
//! 2013-02-05 20:41:46+00 - 2012 February 5th, 20:41:46 UTC
//
//! 2013-02-05 20:41:46,123+00 - 2012 February 5th, 20:41:46 UTC
//
std::istream &operator>>(std::istream &, Time &);

//!
//! Output of ISO 8601 timestamps
//!
std::ostream &operator<<(std::ostream &, const Time &);




//!
//! \class DiffTime
//!
//! A representation of time difference.
//!
class DiffTime {
public:
  DiffTime();

  //! This constructor can be given a number of seconds and
  //! microseconds.
  //
  //! \param sec Number of seconds. May be both positive or
  //! negative.
  //
  //! \param nsec Nanoseconds. This number must not be negative!  The
  //! number is normalized so that if more than a billion nanoseconds
  //! is specified, the number of seconds is correctly updated. So,
  //! specifying 10,000,000,000 in the nsec field will simply
  //! construct a DiffTime of 10 seconds.
  //
  DiffTime(int64_t sec, int64_t nsec);

  DiffTime(const time_internal::time_rec &t) : tim(t) { }

  explicit DiffTime(const double &seconds);
  explicit DiffTime(time_t seconds);

  //! Parse ISO 8601 period designation
  static DiffTime iso(const std::string&);

  // Construct a difftime from a long holding miliseconds
  static DiffTime msec(long msecs) {
    return DiffTime(msecs / 1000.0);
  }

  // Construct a difftime from a long holding seconds
  static DiffTime sec(long secs) {
    return DiffTime(double(secs));
  }

  // conversion routines - returns time in SECONDS
  double to_double() const;
  time_t to_timet() const { return time_t(tim.sec); }

  // clamping
  void zeroIfNegative();

  // Returns the difference formatted as eg. "1 day, 2 hours, 4 minutes, 23 seconds".
  std::string toFormattedString() const;

  //! Negate difference
  const DiffTime operator-() const;
  //! Subtract time differences
  const DiffTime operator-(const DiffTime& p2 ) const;
  //! Add time differences
  const DiffTime operator+(const DiffTime& p2 ) const;
  //! Subtract and assign
  DiffTime& operator-=(const DiffTime& p2 );
  //! Add and assign
  DiffTime& operator+=(const DiffTime& p2 );
  //! Scale time difference
  DiffTime operator*(double scale) const;

  //
  // Time compare operators
  //
  bool operator<(const DiffTime& p2) const;
  bool operator>(const DiffTime& p2) const;
  bool operator<=(const DiffTime& p2) const;
  bool operator>=(const DiffTime& p2) const;
  bool operator==(const DiffTime& p2) const;

  //
  //! Constants
  //
  static const DiffTime SECOND, MSEC, USEC, NSEC;
  static const DiffTime ENDLESS;

  friend class Time;
private:
  //! Our actual time span
  time_internal::time_rec tim;
};




//!
//! Parsing of ISO 8601 durations
//
//! We support the ISO 8601 duration format with years, months, days,
//! hours, minutes and seconds - we do not support the duration format
//! with weeks.
//
//! Examples follow:
//
//! P<n>Y<n>M<n>DT<n>H<n>M<n>S
//
//! P1DT40M2S - 1 day 40 minutes and 2 seconds
//
//! Because only the second is non-ambiguous (a minute may have 60 or
//! 61 seconds, a month may have 28, 29, 30 or 31 days, a year may
//! have 365 or 366 days), when we output durations we only output
//! PT<n>S because it is the only non-ambiguous ISO 8601 format.
//
//! When parsing intervals, we make the naïve assumption that a year
//! is 365 days, a month is 30 days, a day is 24 hours and a minute is
//! 60 seconds.
//
std::istream &operator>>(std::istream &, DiffTime &);

//!
//! Output of ISO 8601 durations
//!
std::ostream &operator<<(std::ostream &, const DiffTime &);


# include "time.hcc"

#endif

//
//! \file common/time.cc
//
//! Implementation of our time routines
//
// $Id: time.cc,v 1.17 2013/05/23 08:54:57 joe Exp $
//

#include "time.hh"
#include "error.hh"

#include "string.hh"
#include <vector>
#include <iomanip>
#include <istream>
#include <ostream>

#include <math.h>
#include <time.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
# include <sys/time.h>
#endif

Time Time::now()
{
#if defined(__unix__) || defined(__APPLE__)
  timeval tv;
  if (gettimeofday(&tv,0))
    throw syserror("gettimeofday", "retrieval of now");
  return Time(tv.tv_sec,tv.tv_usec * 1000);
#endif

#if defined(_WIN32)
  SYSTEMTIME st;
  GetSystemTime(&st);
  FILETIME ft;
  if (SystemTimeToFileTime(&st,&ft) == 0) {
    std::cerr << "(TIME) Error getting system time " << std::endl;
    std::cerr << "(TIME) This may cause inaccurate measurements "
              << std::endl;
  };

  /* tf holds  a 64 bit time in it's 2 fields
     the number represents 100-nanosecs. since jan. 1 1601
  */
  uint64_t sec,nsec;

  uint64_t offset = uint64_t(10000000)
    * ((1970 - 1601) * 365 + 89) * 24 * 60 * 60;
  uint64_t file_time = (uint64_t(ft.dwHighDateTime) << 32) + ft.dwLowDateTime;
  file_time -= offset;
  sec = file_time / 10000000;
  nsec = (file_time % 10000000) * 100;

  return Time(sec, nsec);
#endif
}

#ifdef _WIN32
Time::Time(const FILETIME &ft)
  : tim(0,0)
{
  uint64_t offset = uint64_t(10000000)
    * ((1970 - 1601) * 365 + 89) * 24 * 60 * 60;
  uint64_t file_time = (uint64_t(ft.dwHighDateTime) << 32) + ft.dwLowDateTime;
  file_time -= offset;
  tim.sec = file_time / 10000000;
  tim.nsec = (file_time % 10000000) * 100;
}

FILETIME Time::toFileTime() const
{
  FILETIME file_time;
  uint64_t offset = uint64_t(10000000)
    * ((1970 - 1601) * 365 + 89) * 24 * 60 * 60;
  uint64_t nano_secs = uint64_t(tim.sec) * 10000000 + uint64_t(tim.nsec) + offset;
  file_time.dwLowDateTime = DWORD(nano_secs);
  file_time.dwHighDateTime = nano_secs >> 32;
  return file_time;
}
#endif

#ifdef __unix__
struct timespec Time::toTimespec() const
{
  struct timespec ret;
  ret.tv_sec = tim.sec;
  ret.tv_nsec = tim.nsec;
  return ret;
}
#endif

double Time::to_double() const
{
  return tim.sec + tim.nsec / 1E9;
}


//
// Utility stuff for the ISO8601 parser
//
namespace {

  // Reads exactly d characters from stream and parses unsigned
  // integer
  unsigned readDigits(std::istream &i, size_t d)
  {
    std::vector<char> buf(d);
    i.read(&buf[0], d);
    if (i.eof())
      throw error("ISO8601: Premature end of data while reading digits");
    // Fine, now parse as integer
    return string2Any<unsigned>(std::string(buf.begin(), buf.end()));
  }


  // Reads a single character and returns it
  char getChar(std::istream &i)
  {
    if (i.eof())
      throw error("ISO8601: End of stream before get");
    char tmp;
    i.get(tmp);
    if (i.gcount() != 1)
      throw error("ISO8601: Premature end of data while getting character");
    return tmp;
  }

  // Reads a character and sees if it matches the test character.  If
  // it does, the function returns true. If it does not, it puts back
  // the character onto the stream and returns false.
  bool testChar(std::istream &i, char test)
  {
    char t = getChar(i);
    if (t == test) return true;
    i.putback(t);
    return false;
  }


  // Reads a single character and expects it to match given character
  void skipKnown(std::istream &i, char known)
  {
    char tmp;
    i.get(tmp);
    if (i.gcount() != 1)
      throw error("ISO8601: Premature end of data while skipping character");
    if (tmp != known)
      throw error("ISO8601: Unexpected character while parsing");
  }

  // Reads a ISO8601 possibly-fractional number from the stream
  double readFractional(std::istream &i)
  {
    // Read digits and accept both '.' and ',' as fraction
    bool gotsome = false;
    double res = 0;
    size_t divider = 0;
    while (true) {
      char c = getChar(i);
      if (c >= '0' && c <= '9') {
        if (divider) {
          res += (c - '0') / double(divider);
          divider *= 10;
        } else {
          res *= 10;
          res += c - '0';
        }
        gotsome = true;
        continue;
      }
      if (!divider && (c == '.' || c == ',')) {
        divider = 10;
        gotsome = true;
        continue;
      }
      // Ok, not a digit and not (first) fractional
      i.putback(c);
      break;
    }
    if (!gotsome)
      throw error("ISO8601: Not a fractional");
    return res;
  }

}

#if defined(__sun__)
// Solaris doesn't have the GNU specific timegm function, so we
// implement it here using mktime() and gmtime(). Would be nice to
// have it static, but mktime_utc_test uses this function as well.
time_t timegm(struct tm* t)
{
  time_t tl, tb;
  struct tm* tg;

  tl = mktime(t);
  if (tl == -1)
    return -1;
  tg = gmtime(&tl);
  if (!tg)
    return -1;
  tb = mktime(tg);
  if (tb == -1)
    return -1;

  return (tl - (tb - tl));
}
#endif

std::istream &operator>>(std::istream &i, Time &o)
{
  struct tm t;
  memset(&t, 0, sizeof t);

  // YYYY-MM-DD
  t.tm_year = readDigits(i, 4) - 1900;
  skipKnown(i, '-');
  t.tm_mon = readDigits(i, 2) - 1;
  skipKnown(i, '-');
  t.tm_mday = readDigits(i, 2);
  // T or space
  { char sep = getChar(i);
    if (sep != 'T' && sep != ' ')
      throw error("Unexpected separator between date and time");
  }
  // hh:mm:ss
  t.tm_hour = readDigits(i, 2);
  skipKnown(i, ':');
  t.tm_min = readDigits(i, 2);
  skipKnown(i, ':');
  t.tm_sec = readDigits(i, 2);

  // Optionally we have fractional seconds - ISO 8601 specifies that
  // both ',' and '.' can be used to designate the fraction
  char trailer = getChar(i);
  do {
    if (trailer != '.' && trailer != ',')
      break;
    // Good, we have fractional seconds... Fetch them and throw them
    // away
    do {
      trailer = getChar(i);
    } while (trailer >= '0' && trailer <= '9');
  } while (false);

  // Assume Zulu time - either 'Z' or '+00'
  do {
    if (trailer == 'Z') break;
    if (trailer != '+')
      throw error("Expected Z or +00");
    skipKnown(i, '0');
    skipKnown(i, '0');
  } while (false);

  // Just peek at the next character - this has the effect of
  // setting eof if we are really at the eof.
  i.peek();

  // Fine, now convert
#if defined (__unix__) || defined(__APPLE__)
  o = Time(timegm(&t));
#endif

#if defined(_WIN32)
  o = Time(_mkgmtime(&t));
#endif

  return i;
}

std::ostream &operator<<(std::ostream &s, const Time &ts)
{
  struct tm t;

#if defined(__unix__) || defined(__APPLE__)
  const time_t ut(ts.to_timet());
  if (!gmtime_r(&ut, &t))
    throw error("Cannot output time");
#endif

#if defined(_WIN32)
  const time_t in(ts.to_timet());
  if (gmtime_s(&t, &in))
    throw error("Cannot output time");  
#endif

  s << std::setw(4) << std::setfill('0') << (t.tm_year + 1900)
    << "-"
    << std::setw(2) << std::setfill('0') << (t.tm_mon + 1)
    << "-"
    << std::setw(2) << std::setfill('0') << t.tm_mday
    << "T"
    << std::setw(2) << std::setfill('0') << t.tm_hour
    << ":"
    << std::setw(2) << std::setfill('0') << t.tm_min
    << ":"
    << std::setw(2) << std::setfill('0') << t.tm_sec
    << "Z";
  return s;
}


const DiffTime Time::operator-(const Time& p2 ) const {
  // This calculation will never overflow, as Time contains only
  // positive values, whereas DiffTime allows the full signed scale.
  time_internal::time_rec res(tim);
  res.sec -= p2.tim.sec;
  res.nsec -= p2.tim.nsec;
  if (res.nsec < 0) {
    res.sec--;
    res.nsec += 1000000000;
  }
  if (res.nsec < 0)
    throw error("Time difference subtraction gave negative nsec");
  return res;
}

const Time Time::operator-(const DiffTime& p2 ) const
{
  return Time(*this) -= p2.tim;
}

const Time Time::operator+(const DiffTime& p2 ) const
{
  return Time(*this) += p2.tim;
}

Time& Time::operator+=(const DiffTime& p2 )
{
  time_internal::time_rec res(tim);
  res.sec += p2.tim.sec;
  res.nsec += p2.tim.nsec;
  if (res.nsec >= 1000000000) {
    res.sec++;
    res.nsec -= 1000000000;
  }
  if (res.nsec >= 1000000000)
    throw error("Addition between non-normalized time and time difference");
  // Check for overflows
  if (p2.tim.sec >= 0) {
    // Adding a positive value, res must have increased
    if (res.sec < tim.sec) {
      // sec has wrapped, clamp at END_OF_TIME
      *this = END_OF_TIME;
    }
  } else {
    // Adding a negative value, res must have decreased
    if (res.sec > tim.sec) {
      // sec has wrapped, clamp at BEGINNING_OF_TIME
      *this = BEGINNING_OF_TIME;
    }
  }
  tim = res;
  return *this;
}

const Time Time::BEGINNING_OF_TIME(0, 0);
const Time Time::END_OF_TIME(0x7FFFFFFFFFFFFFFFLL, 0);

//
//  Implementation of class DiffTime
//

DiffTime::DiffTime(const double& dt)
  : tim(int64_t(floor(dt)), int64_t((dt - floor(dt)) * 1E9))
{
}

DiffTime::DiffTime(time_t dt)
  : tim(dt, 0)
{
}

DiffTime::DiffTime(int64_t sec, int64_t nsec)
  : tim(sec, nsec)
{
  // If too many microseconds specified, add whole seconds to sec
  // and let usec contain the microseconds.
  if (tim.nsec >= 1000000000) {
    const lldiv_t res = lldiv(tim.nsec, 1000000000);
    tim.sec += res.quot;
    tim.nsec = res.rem;
  }
  if (tim.nsec < 0)
    throw error("Cannot normalize negative nanoseconds");
}

double DiffTime::to_double() const
{
  return tim.sec + tim.nsec / 1E9;
}

void DiffTime::zeroIfNegative()
{
  if (*this < DiffTime(0,0))
    *this = DiffTime(0,0);
}


std::string DiffTime::toFormattedString() const
{
  std::ostringstream out;
  if (int64_t days = tim.sec / 60 / 60 / 24)
    out << days << " days ";
  if (int64_t hours = tim.sec / 60 / 60 % 24)
    out << hours << " hours ";
  if (int64_t minutes = tim.sec / 60 % 60)
    out << minutes << " minutes ";
  if (int64_t seconds = tim.sec % 60)
    out << seconds << " seconds ";
  return out.str();
}

const DiffTime DiffTime::operator-() const
{
  if (tim.nsec) {
    return DiffTime(-tim.sec - 1, 1000000000 - tim.nsec);
  } else {
    return DiffTime(-tim.sec, 0);
  }
}

const DiffTime DiffTime::operator-(const DiffTime& p2 ) const
{
  return DiffTime(*this) -= p2.tim;
}

const DiffTime DiffTime::operator+(const DiffTime& p2 ) const
{
  return DiffTime(*this) += p2.tim;
}

DiffTime& DiffTime::operator-=(const DiffTime& p2 )
{
  return *this += -p2;
}

DiffTime& DiffTime::operator+=(const DiffTime& p2 )
{
  time_internal::time_rec res(tim);
  res.sec += p2.tim.sec;
  res.nsec += p2.tim.nsec;
  if (res.nsec >= 1000000000) {
    res.sec++;
    res.nsec -= 1000000000;
  }
  if (res.nsec >= 1000000000)
    throw error("Sum of non-normalized difftimes - nsec too big");
  // Check for overflows
  if (p2.tim.sec >= 0) {
    // Adding a positive value, res must have increased
    if (res.sec < tim.sec) {
      // sec has wrapped, clamp at END_OF_TIME
      *this = ENDLESS;
    }
  } else {
    // Adding a negative value, res must have decreased
    if (res.sec > tim.sec) {
      // sec has wrapped, clamp at BEGINNING_OF_TIME
      *this = -ENDLESS;
    }
  }
  tim = res;
  return *this;
}

DiffTime DiffTime::operator*(double scale) const
{
  return DiffTime(this->to_double() * scale);
}

bool DiffTime::operator <(const DiffTime& p2) const {
  return to_double() < p2.to_double();
}
bool DiffTime::operator >(const DiffTime& p2) const {
  return p2 < *this;
}
bool DiffTime::operator <=(const DiffTime& p2) const {
  return !(p2 < *this);
}
bool DiffTime::operator >=(const DiffTime& p2) const {
  return !(*this < p2);
}

bool DiffTime::operator==(const DiffTime& p2) const
{
  return tim.sec == p2.tim.sec
    && tim.nsec == p2.tim.nsec;
}

DiffTime DiffTime::iso(const std::string &iso)
{
  std::istringstream i(iso);
  DiffTime tmp;
  i >> tmp;
  return tmp;
}


const DiffTime DiffTime::SECOND(1, 0);
const DiffTime DiffTime::MSEC(0, 1000000);
const DiffTime DiffTime::USEC(0, 1000);
const DiffTime DiffTime::NSEC(0, 1);
const DiffTime DiffTime::ENDLESS(0x7FFFFFFFFFFFFFFFLL, 0);


std::istream &operator>>(std::istream &i, DiffTime &o)
{
  double secs = 0;
  bool gotsome = false;

  if (getChar(i) != 'P')
    throw error("Not a ISO8601 period designator");

  // We may run into a 'T' - but until we do, we parse years, months
  // and days.
  while (true) {
    // Read some number
    double n;
    try { n = readFractional(i); }
    catch (error &) { break; }
    gotsome = true;
    // Get the designator
    switch (char c = getChar(i)) {
    case 'Y': secs += 365 * 24 * 3600 * n; break;
    case 'M': secs += 30 * 24 * 3600 * n; break;
    case 'D': secs += 24 * 3600 * n; break;
    default:
      throw error("Unexpected designator '"
                  + std::string(1, c) + "' in left side of ISO 8601 period");
    }
  }

  if (testChar(i, 'T')) {
    // Fine, we got our T (or we failed reading the next
    // fractional). Now read hours, minutes and seconds.
    while (true) {
      // Read some number
      double n;
      try { n = readFractional(i); }
      catch (error &) { break; }
      gotsome = true;
      // Get the designator
      switch (char c = getChar(i)) {
      case 'H': secs += 60 * 60 * n; break;
      case 'M': secs += 60 * n; break;
      case 'S': secs += n; break;
      default:
        throw error("Unexpected designator '"
                    + std::string(1, c) + "' in right side of ISO 8601 period");
      }
    }
  }

  if (!gotsome)
    throw error("No ISO8601 period data after period designator");

  o = DiffTime(secs);
  return i;
}


std::ostream &operator<<(std::ostream &s, const DiffTime &ts)
{
  s << "PT" << ts.to_double() << "S";
  return s;
}


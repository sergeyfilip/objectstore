//
// XML input and output generator templates
//
// $Id: xmlio.hh,v 1.6 2013/09/02 13:53:26 joe Exp $
//

#ifndef XML_XMLIO_HH
#define XML_XMLIO_HH

#include <istream>
#include <list>

namespace xml {

  class XMLexer {
  public:
    XMLexer(std::istream&);

    //! Returns the next few handfulls of bytes from the input; meant
    //! to be used when formatting error messages
    std::string getCurrentContext() const;

    //! Returns true if end-of-input
    bool endOfStream() const;

    //! Reads raw bytes from input and matches them up against the
    //! given string. Returns true on match (and will have consumed
    //! the bytes read) - returns false on mismatch (and will not
    //! consume data)
    bool matchRaw(const std::string&) const;

    //! We must use this member function to read out raw data from the
    //! input stream; this ensures that state can be properly backed
    //! up
    char getRaw() const;

    //! Read formatted data from the input stream and match up against
    //! the argument given.
    bool match(const std::string&) const;

    //! We must use this member function to read out formatted data
    //! ('formatted' means; if the input stream contains a "&gt;"
    //! sequence then we will return a '<' character) from the input
    //! stream; this ensures that state can be properly backed up
    char get() const;




    //! Note; [Sec 2.11] newline handling


    //! Backup of input stream state. Instantiate a backup object to
    //! attempt parsing something; call restore to restore previous
    //! lexer state
    class Backup {
    public:
      //! Instantiate a backup on a lexer
      Backup(const XMLexer& in);
      //! Destroy a backup on a lexer
      ~Backup();
      //! Call this to restore the lexer state to where we were
      //! instantiated
      void restore();
      //! Called by XMLexer to add data to the backup set
      void addData(char c);
      //! For diagnostics it is useful to be able to inspect the data
      //! buffer
      const std::string& getData() const { return data; }
    private:
      //! If we're not the top-most backup on the backup stack and the
      //! top-most backup is restoring, we need to drop characters
      //! from our restore buffer as well.
      void dropData();
      //! There can be a stack of backup objects. This pointer points
      //! to the previous most-recent (deepest stack) backup, or it is
      //! a null pointer if this is the root backup object.
      Backup* prev_backup;
      //! A reference to our lexer
      const XMLexer& lexer;
      //! This is all characters that were consumed from the lexer
      //! while we existed
      std::string data;
    };

  private:

    //! Our input stream
    std::istream& input;

    //! Either this is a null pointer or it is a pointer to the
    //! deepest Backup object we have. We want to append all
    //! modifications that are made to the deepest backup object at
    //! any time.
    mutable Backup* deepest_backup;
  };

  //! This is our XML writer class. It helps with printing of data to
  //! XML files. All strings given to the writer are UTF-8
  //! encoded. This class will take care of whatever other encoding
  //! needs doing.
  class XMLWriter {
  public:
    //! Construct the writer with a stream it can write to
    XMLWriter(std::ostream&);

    //! Write verbatim - no conversion... Used to output prolog from
    //! Document, element markup etc.
    void writeVerbatim(const std::string& data);

    //! Write an element or attribute name. Throws if name contains
    //! invalid characters.
    void writeName(const std::string& name);

    //! Write CharData. This routine will properly encode whatever
    //! characters need special encoding ('<' -> &lt; and so on). Will
    //! throw if data contains invalid characters.
    void writeCharData(const std::string& chardata);

  private:
    //! Our output stream
    std::ostream& output;

    //! Output a NameStartChar
    void writeNameStartChar(const std::string& );

    //! Chop the first character off a UTF-8 string and return it
    std::string utf8GetFirst(std::string&);
  };



  class IDocument {
  public:
    virtual ~IDocument() { }

    //! Parse the XML document
    virtual void process(XMLexer& in) const = 0;

    //! Generate the XML document
    virtual void output(XMLWriter& out) const = 0;

    //! Output our RNC schema
    virtual std::string schemaRNC() const = 0;

  };



  //! The top level document.
  //
  //! [Sec 2.1] Document ::= prolog element Misc*
  //
  //! prolog     ::= XMLDecl? Misc* (doctypedecl  Misc*)?
  //! XMLDecl    ::= <?xml' VersionInfo EncodingDecl? SDDecl? S? '?>'
  //! VersionInfo::= S 'version' Eq ("'" VersionNum "'" | '"' VersionNum '"')
  //! Eq         ::= S? '=' S?
  //! VersionNum ::= '1.' [0-9]+
  //! Misc       ::= Comment | PI | S
  //! EncodingDecl ::= S 'encoding' Eq ('"' EncName '"' | "'" EncName "'" )
  //! EncName    ::= [A-Za-z] ([A-Za-z0-9._] | '-')*

  template <class TElement>
  class Document : public IDocument {
  public:
    Document(const TElement& element_) : element(element_) { }

    //! Attempt parsing the document
    void process(XMLexer& in) const;

    //! Parse an *optional* XMLDecl
    void processXMLDecl(XMLexer& in) const;

    //! Output the docment - throws on error
    void output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC() const;

  private:
    TElement element;
  };


  //! Utility routine to construct a document. This saves us from
  //! having to type the type of the TElement
  template <class TElement>
  Document<TElement> mkDoc(const TElement& e) { return Document<TElement>(e); }


  //! [Sec 2.2] S ::= (#x20 | #x9 | #xD | #xA)+
  class SymSpace {
  public:
    //! Nothing to construct really
    SymSpace() { }

    //! Attempt to parse a space
    bool process(XMLexer& in) const;

  };

  //! [Sec 2.3]
  //
  //! NameStartChar ::= ":" | [A-Z] | "_" | [a-z] | [#xC0-#xD6]
  //!                   | [#xD8-#xF6] | [#xF8-#x2FF] | [#x370-#x37D]
  //!                   | [#x37F-#x1FFF] | [#x200C-#x200D]
  //!                   | [#x2070-#x218F] | [#x2C00-#x2FEF]
  //!                   | [#x3001-#xD7FF] | [#xF900-#xFDCF]
  //!                   | [#xFDF0-#xFFFD] | [#x10000-#xEFFFF]
  //! NameChar      ::= NameStartChar | "-" | "." | [0-9] | #xB7
  //!                   | [#x0300-#x036F] | [#x203F-#x2040]
  //! Name          ::= NameStartChar (NameChar)*
  //! Names         ::= Name (#x20 Name)*
  //! Nmtoken       ::= (NameChar)+
  //! Nmtokens      ::= Nmtoken (#x20 Nmtoken)*
  class SymName {

  };

  template <class A, class B>
  class EOrderedSequence;
  template <class A, class B>
  class EUnorderedSequence;
  template <class E, class S>
  class EGroup;
  template <class E, class Binder>
  class EAction;
  template <class A, class B>
  class EOption;
  template<class E>
  class EOptional;
  template<class E>
  class ERepeated;


  //! Our base Element parser
  template <class Derived>
  class ElementParser {
  public:
    //! Compile-time up-cast (CRTP) used when combining parsers
    Derived& derived() {
      return *static_cast<Derived*>(this);
    }

    //! Compile-time up-cast (CRTP) used when combining parsers
    const Derived& derived() const {
      return *static_cast<const Derived*>(this);
    }

    //! Grouping.
    template <class S>
    EGroup<Derived,S> operator()(const S& sub) const {
      return EGroup<Derived,S>(derived(), sub);
    }

    //! Set an action on successful parse... Used for repeating
    //! groups. The Binder object given must implement the following
    //! method:
    //
    //!  void operator()() const;
    //
    //! The Closure classes in Common/cpp/typeutil.hh do this, so
    //! using the papply() methods to generate a binder for an action
    //! seems like an obvious choice... For example:
    //
    //! Element("foo")(CharData(data))[papply(this, &F::do, data)]
    //
    template <class Binder>
    EAction<Derived,Binder> operator[](const Binder& b_) const {
      return EAction<Derived,Binder>(derived(), b_);
    }

    //! Make an Element optional. We use the C++ prefix '!' operator
    //! instead of the EBNF postfix '?' operator.
    EOptional<Derived> operator!() {
      return EOptional<Derived>(derived());
    }

    //! Sequence generation; we use the C++ comma operator for RNC
    //! comma. Note that you will need extra parenthesis to distinguish
    //! this comma from argument separator commas.
    template <class B>
    EOrderedSequence<Derived,B> operator,(const B& b_)  {
      return EOrderedSequence<Derived,B>(derived(), b_);
    }

    //! Option generation; we use the C++ pipe operator for RNC
    //! pipe.
    template <class B>
    EOption<Derived,B> operator|(const B& b_) {
      return EOption<Derived,B>(derived(), b_);
    }

    //! Unordered sequence. We depend upon the fact that this operator
    //! associates left-to-right in our implementation of the
    //! EUnorderedSequence matching. In other words; "a & b & c" must
    //! end up as "match( match(a, b), c )"
    template <class B>
    EUnorderedSequence<Derived,B> operator&(const B& b_)  {
      return EUnorderedSequence<Derived,B>(derived(), b_);
    }

    //! The C++ prefix '*' is used instead of EBNF postfix '*'.
    //
    //! Example:    *(Element("foo"))
    ERepeated<Derived> operator*() {
      return ERepeated<Derived>(derived());
    }

  };


  //! [Sec 2.5] Comment	::= '<!--' ((Char - '-') | ('-' (Char - '-')))* '-->'
  class SymComment : public ElementParser<SymComment> {
  public:
    SymComment(std::string& dest_)
      : dest(dest_), seqmatch(false) { }

    //! Parse the element
    bool process(XMLexer& in) const;

    //! Output the element
    void output(XMLWriter& out) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;

  private:
    //! Destination data
    std::string& dest;

    //! Our sequence match state variable
    mutable bool seqmatch;
  };

  //! CharData - this class is used in conjunction with elements.
  //
  //! [Sec 2.4] CharData ::= [^<&]* - ([^<&]* ']]>' [^<&]*)
  //
  //! Also handles [Sec 2.7] CDATA, as CDATA sections may occur
  //! anywhere character data may occur.
  //
  //! CDSect  ::=  CDStart  CData  CDEnd
  //! CDStart ::= '<![CDATA['
  //! CData   ::= (Char* - (Char* ']]>' Char*))
  //! CDEnd   ::= ']]>'
  //
  // There is special handling when Destination is a std::string.
  //
  // In all other cases, the Destination type must implement
  // 'std::string toString() const' and 'fromString(const
  // std::string&)'
  //
  template <class Destination>
  class CharData : public ElementParser<CharData<Destination> > {
  public:
    CharData(Destination& dest_)
      : dest(dest_), seqmatch(false) { }

    //! Parse the element
    bool process(XMLexer& in) const;

    //! Output the element
    void output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;

  private:
    //! Destination data
    Destination& dest;

    //! Our sequence match state variable
    mutable bool seqmatch;
  };


  //! We can have entire sub-documents. Basically this is an element
  //! with any number of nested elements and contents.
  //
  //! We store the raw string here - it must be parsed separately if
  //! so desired.
  class SubDocument : public ElementParser<SubDocument> {
  public:
    SubDocument(std::string & d_)
      : dest(d_), seqmatch(false) { }

    //! Parse the element
    bool process(XMLexer& in) const;

    //! Output the element
    void output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;

  private:
    //! Destination data
    std::string& dest;

    //! Our sequence match state variable
    mutable bool seqmatch;
  };


  //! Element;  a named element
  //
  //! [Sec 3] element ::= EmptyElemTag
  //!                     | STag content ETag
  //! [Sec 3.1]
  //!  STag ::= '<' Name (S  Attribute)* S? '>'
  //!  Attribute ::= Name Eq AttValue
  //!  ETag ::= '</' Name  S? '>'
  //!  content ::= CharData? ((element | Reference | CDSect
  //!                          | PI | Comment) CharData?)*
  //!  EmptyElemTag ::= '<' Name (S  Attribute)* S? '/>'
  //!
  class Element : public ElementParser<Element> {
  public:
    Element(const std::string& name_)
      : name(name_), seqmatch(false) { }

    //! Parse the element
    bool process(XMLexer& in) const;

    //! Output the element
    void output(XMLWriter& out) const;

    //! Parse an empty-element tag
    bool processEETag(XMLexer& in) const;

    //! Parse a start tag
    bool processSTag(XMLexer& in) const;

    //! Parse an end tag
    bool processETag(XMLexer& in) const;

    //! Output a start tag
    void outputSTag(XMLWriter& out) const;

    //! Output an end tag
    void outputETag(XMLWriter& out) const;

    //! Output an end tag
    void outputEETag(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;

  private:
    //! The element name we must match
    const std::string name;

    //! Our sequence match state variable
    mutable bool seqmatch;
  };


  //! We can have optional elements; either they are there or they are
  //! not
  template <class EType>
  class EOptional : public ElementParser<EOptional<EType> > {
  public:
    EOptional(const EType& e) : m_element(e), seqmatch(false) { }

    //! Parse the element
    bool process(XMLexer& in) const;

    //! Output the element
    void output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;

  private:
    EType m_element;

    //! Our sequence match state variable
    mutable bool seqmatch;
  };


  //! Ordered sequence:   if (a(t) && b(t))
  template <class A, class B>
  class EOrderedSequence : public ElementParser<EOrderedSequence<A,B> > {
  public:
    EOrderedSequence(const A& a_, const B& b_)
      : a(a_), b(b_), seqmatch(false) { }

    //! Parse the element sequence
    bool process(XMLexer& in) const;

    //! Output the element
    void output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;

  private:
    A a;
    B b;

    //! Our sequence match state variable
    mutable bool seqmatch;
  };


  /// Unordered sequence
  template <class A, class B>
  class EUnorderedSequence : public ElementParser<EUnorderedSequence<A,B> > {
  public:
    EUnorderedSequence(const A& a_, const B& b_)
      : a(a_), b(b_) { }

    //! Parse the element sequence
    bool process(XMLexer& in) const;

    //! Output the element
    void output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! We want to try matching independently on our left and right
    //! side, let either one of them improve the match. Order matters
    //! not.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear match for subs
    void seqClearmatch() const;

  private:
    A a;
    B b;
  };


  //! A group is an element that holds other elements.
  //
  //! In RNC it would look like
  //!   element my_group { element foo {text}, element bar {text} }
  //! In our notation it would look like
  //!   eGroup((eFoo, eBar))
  template <class E, class S>
  class EGroup : public ElementParser<EGroup<E,S> > {
  public:
    EGroup(const E& e, const S& s)
      : element(e), sub(s), seqmatch(false) { }

    //! Parse the group
    bool process(XMLexer& in) const;

    //! Output the element
    void output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;

  private:
    E element;
    S sub;

    //! Our sequence match state variable
    mutable bool seqmatch;
  };


  //! We can have lists of one given element type
  template <class EType>
  class ERepeated : public ElementParser<ERepeated<EType> > {
  public:
    ERepeated(const EType& e)
      : element(e), seqmatch(false) { }

    //! We are successful if the element parses zero or more times.
    bool process(XMLexer& in) const;

    //! Output the element
    void output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;

  private:
    EType element;

    //! Our sequence match state variable
    mutable bool seqmatch;
  };


  //! An Action on a successfully parsed element...
  template <class E, class Binder>
  class EAction : public ElementParser<EAction<E,Binder> > {
  public:
    EAction(const E& e_, const Binder& b_)
      : element(e_), binder(b_) { }

    //! Parse the element. On success, run the binder. If the binder
    //! returns false we fail the processing
    bool process(XMLexer& in) const;

    //! Output the element - returns true if it should be re-called by
    //! repeater
    bool output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;

  private:
    E element;
    Binder binder;
  };

  //! An Option on a successfully parsed element - as in A|B|C
  template <class A, class B>
  class EOption : public ElementParser<EOption<A,B> > {
  public:
    EOption(const A& a_, const B& b_)
      : m_a(a_), m_b(b_) { }

    //! Parse the element.
    bool process(XMLexer& in) const;

    //! Output the element
    //! We only output the first option. This is not correct and a
    //! proper solution will be needed when womeone needs to use this
    //! for real.
    void output(XMLWriter& out) const;

    //! RNC schema output
    std::string schemaRNC(unsigned) const;

    //! For unordered sequence matching we want to be able to attempt
    //! to match the current element, updating our match state
    //! variable if we improved it. We return true on progress, false
    //! otherwise.
    bool seqTrymatch(XMLexer& in) const;

    //! Deduce whether we matched or not
    bool seqGetmatch() const;

    //! Clear the sequence match state variable
    void seqClearmatch() const;
  private:
    A m_a;
    B m_b;
  };


  //
  // Parsing utility class: For parsing sequences of elements it is
  // useful to have this "sequencer" which will simply add whatever
  // element type we are parsing to a list of such elements
  //
  template <class E>
  struct sequencer {
    bool add();
    E m_tmp;
    typedef std::list<E> list_t;
    list_t m_list;
  };


}

# include "xmlio.hcc"

#endif

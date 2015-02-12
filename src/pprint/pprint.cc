#include "pprint/pprint.hpp"

#include <vector>
#include <list>
#include <functional>
#include <memory>

#include "errors.hpp"
#include <boost/optional.hpp>

namespace pprint {

// Pretty printing occurs in two global phases.  Rather than try to
// print some random C++ tree directly, which could get quite ugly
// quite quickly, we build a "pretty printer document" out of some
// very simple primitives.  These primitives (and our algorithm) are
// due to Oppen[1] originally and later Kiselyov[2].  Oppen's original
// formulation had `Text`, `LineBreak`, `Concat`, and `Group`.  I
// generalized `LineBreak` to `cond_t` which became our `cond_t`
// because we need to do more sophisticated breaks, and I added
// `nest_t` for controllable indentation.
//
// [1]: Oppen, D.C.: Prettyprinting. ACM Trans. Program. Lang. Syst. 2
//      (1980) 465–483.  Not available online without an ACM subscription.
//
// [2]: Kiselyov, O., Petyon-Jones, S. and Sabry, A.: Lazy v. Yield:
//      Incremental, Linear Pretty-printing.  Available online at
//      http://okmij.org/ftp/continuations/PPYield/yield-pp.pdf

class text_t;
class cond_t;
class concat_t;
class group_t;
class nest_t;

class document_visitor_t {
public:
    virtual ~document_visitor_t() {}

    virtual void operator()(const text_t &) const = 0;
    virtual void operator()(const cond_t &) const = 0;
    virtual void operator()(const concat_t &) const = 0;
    virtual void operator()(const group_t &) const = 0;
    virtual void operator()(const nest_t &) const = 0;
};

class text_t : public document_t {
public:
    std::string text;

    text_t(const std::string &str) : text(str) {}
    text_t(std::string &&str) : text(str) {}
    virtual ~text_t() {}

    virtual unsigned int width() const { return text.length(); }
    virtual void visit(const document_visitor_t &v) const { v(*this); }
    virtual std::string str() const { return "Text(\"" + text + "\")"; }
};

doc_handle_t make_text(const std::string text) {
    return make_counted<text_t>(std::move(text));
}

class cond_t : public document_t {
public:
    std::string small, cont, tail;

    cond_t(const std::string l, const std::string r, const std::string t="")
        : small(std::move(l)), cont(std::move(r)), tail(std::move(t)) {}
    virtual ~cond_t() {}

    // no linebreaks, so only `small` is relevant
    virtual unsigned int width() const { return small.length(); }
    virtual std::string str() const {
        return "Cond(\"" + small + "\",\"" + cont + "\",\"" + tail + "\")";
    }

    virtual void visit(const document_visitor_t &v) const { v(*this); }
};

doc_handle_t make_cond(const std::string l, const std::string r,
                       const std::string t) {
    return make_counted<cond_t>(std::move(l), std::move(r), std::move(t));
}

// concatenation of multiple documents
class concat_t : public document_t {
public:
    std::vector<doc_handle_t> children;

    concat_t(std::vector<doc_handle_t> args)
        : children(std::move(args)) {}
    template <typename It>
    concat_t(const It &begin, const It &end)
        : children(begin, end) {}
    concat_t(std::initializer_list<doc_handle_t> init)
        : children(std::move(init)) {}
    virtual ~concat_t() {}

    virtual unsigned int width() const {
        unsigned int w = 0;
        for (const auto &child : children) {
            w += child->width();
        }
        return w;
    }
    virtual std::string str() const {
        std::string result = "";
        for (const auto &child : children) {
            result += child->str();
        }
        return result;
    }

    virtual void visit(const document_visitor_t &v) const { v(*this); }
};

doc_handle_t make_concat(std::vector<doc_handle_t> args) {
    return make_counted<concat_t>(std::move(args));
}
doc_handle_t make_concat(std::initializer_list<doc_handle_t> args) {
    return make_counted<concat_t>(std::move(args));
}
template <typename It>
doc_handle_t make_concat(const It &begin, const It &end) {
    return make_counted<concat_t>(begin, end);
}

class group_t : public document_t {
public:
    doc_handle_t child;

    group_t(doc_handle_t doc) : child(doc) {}
    virtual ~group_t() {}

    virtual unsigned int width() const {
        return child->width();
    }
    virtual std::string str() const {
        return "Group(" + child->str() + ")";
    }

    virtual void visit(const document_visitor_t &v) const { v(*this); }
};

doc_handle_t make_group(doc_handle_t child) {
    return make_counted<group_t>(child);
}

class nest_t : public document_t {
public:
    doc_handle_t child;

    nest_t(doc_handle_t doc) : child(doc) {}
    virtual ~nest_t() {}

    virtual unsigned int width() const {
        return child->width();
    }
    virtual std::string str() const {
        return "Nest(" + child->str() + ")";
    }

    virtual void visit(const document_visitor_t &v) const { v(*this); }
};

doc_handle_t make_nest(doc_handle_t child) {
    return make_counted<nest_t>(child);
}

const doc_handle_t empty = make_counted<text_t>("");
const doc_handle_t br = make_counted<cond_t>(" ", "");
const doc_handle_t dot = make_counted<cond_t>(".", ".");

doc_handle_t comma_separated(std::initializer_list<doc_handle_t> init) {
    if (init.size() == 0) return empty;
    std::vector<doc_handle_t> v;
    auto it = init.begin();
    v.push_back(*it++);
    for (; it != init.end(); it++) {
        v.push_back(make_counted<text_t>(","));
        v.push_back(br);
        v.push_back(*it);
    }
    return make_nest(make_concat(std::move(v)));
}

doc_handle_t arglist(std::initializer_list<doc_handle_t> init) {
    static const doc_handle_t lparen = make_counted<text_t>("(");
    static const doc_handle_t rparen = make_counted<text_t>(")");
    return make_concat({ lparen, comma_separated(init), rparen });
}

template <typename Container>
doc_handle_t dotted_list_int(Container init) {
    static const doc_handle_t plain_dot = make_counted<text_t>(".");
    if (init.size() == 0) return empty;
    if (init.size() == 1) return make_nest(*(init.begin()));
    std::vector<doc_handle_t> v;
    auto it = init.begin();
    v.push_back(*it++);
    bool first = true;
    for (; it != init.end(); it++) {
        // a bit involved here, because we don't want to break on the
        // first dot (looks ugly)
        if (first) {
            v.push_back(plain_dot);
            first = false;
        } else {
            v.push_back(dot);
        }
        v.push_back(*it);
    }
    // the idea here is that dotted(a, b, c) turns into concat(a,
    // nest(concat(".", b, dot, c))) which means that the dots line up
    // on linebreaks nicely.
    return make_concat({v[0], make_nest(make_concat(v.begin()+1, v.end()))});
}

doc_handle_t dotted_list(std::initializer_list<doc_handle_t> init) {
    return dotted_list_int(init);
}

doc_handle_t funcall(const std::string &name,
                     std::initializer_list<doc_handle_t> init) {
    return make_concat({make_text(name), arglist(init)});
}

doc_handle_t r_dot(std::initializer_list<doc_handle_t> args) {
    static const doc_handle_t r = make_counted<text_t>("r");
    std::vector<doc_handle_t> v;
    v.push_back(r);
    v.insert(v.end(), args.begin(), args.end());
    return dotted_list_int(v);
}

// The document tree is convenient for certain operations, but we're
// going to convert it straightaway into a linear stream through
// essentially an in-order traversal.  We do this because it's easier
// to compute the width in linear time; it's possible to do it
// directly on the tree, but the naive algorithm recomputes the widths
// constantly and a dynamic programming or memorized version is more
// annoying.  This stream has the attractive property that we can
// process it one element at a time, so it does not need to be created
// in its entirety.
//
// Our stream type translates `text_t` to `text_element_t` and
// `cond_t` to `cond_element_t`.  Since we're streaming, the extra
// structure for the `concat_t` goes away.  The tricky ones are groups
// and nests, which must preserve their heirarchy somehow.  We do this
// by wrapping the child contents with a GBeg or here `gbeg_element_t`
// meaning Group Begin and ending with a GEnd or `gend_element_t`
// meaning Group End.  Similarly with `nbeg_element_t` and
// `nend_element_t`.

class text_element_t;
class cond_element_t;
class nbeg_element_t;
class nend_element_t;
class gbeg_element_t;
class gend_element_t;

class stream_element_visitor_t
    : public single_threaded_countable_t<stream_element_visitor_t> {
public:
    virtual ~stream_element_visitor_t() {}

    virtual void operator()(text_element_t &) = 0;
    virtual void operator()(cond_element_t &) = 0;
    virtual void operator()(nbeg_element_t &) = 0;
    virtual void operator()(nend_element_t &) = 0;
    virtual void operator()(gbeg_element_t &) = 0;
    virtual void operator()(gend_element_t &) = 0;
};

// Streaming version of the document tree, suitable for printing after
// much processing.  Because we don't have any constant values here
// due to the `hpos` stuff, this can be `single_threaded_countable_t`.
class stream_element_t : public single_threaded_countable_t<stream_element_t> {
public:
    friend class annotate_stream_visitor_t;
    friend class correct_gbeg_visitor_t;
    boost::optional<size_t> hpos;

    stream_element_t() : hpos() {}
    stream_element_t(size_t n) : hpos(n) {}
    virtual ~stream_element_t() {}

    virtual void visit(stream_element_visitor_t &v) = 0;
    virtual std::string str() const = 0;
    std::string pos_or_not() const {
        return hpos ? std::to_string(*hpos) : "-1";
    }
};

typedef counted_t<stream_element_t> stream_handle_t;

class text_element_t : public stream_element_t {
public:
    std::string payload;

    text_element_t(const std::string &text, size_t hpos)
        : stream_element_t(hpos), payload(text) {}
    text_element_t(const std::string &text)
        : stream_element_t(), payload(text) {}
    virtual ~text_element_t() {}
    virtual std::string str() const {
        return "TE(\"" + payload + "\"," + pos_or_not() + ")";
    }

    virtual void visit(stream_element_visitor_t &v) { v(*this); }
};

class cond_element_t : public stream_element_t {
public:
    std::string small, tail, cont;

    cond_element_t(std::string l, std::string t, std::string r)
        : stream_element_t(), small(std::move(l)), tail(std::move(t)),
          cont(std::move(r)) {}
    cond_element_t(std::string l, std::string t, std::string r, size_t hpos)
        : stream_element_t(hpos), small(std::move(l)), tail(std::move(t)),
          cont(std::move(r)) {}
    virtual ~cond_element_t() {}
    virtual std::string str() const {
        return "CE(\"" + small + "\",\"" + tail + "\",\"" + cont + "\","
            + pos_or_not() + ")";
    }

    virtual void visit(stream_element_visitor_t &v) { v(*this); }
};

class nbeg_element_t : public stream_element_t {
public:
    nbeg_element_t() : stream_element_t() {}
    nbeg_element_t(size_t hpos) : stream_element_t(hpos) {}
    virtual ~nbeg_element_t() {}

    virtual void visit(stream_element_visitor_t &v) { v(*this); }
    virtual std::string str() const {
        return "NBeg(" + pos_or_not() + ")";
    }
};

class nend_element_t : public stream_element_t {
public:
    nend_element_t() : stream_element_t() {}
    nend_element_t(size_t hpos) : stream_element_t(hpos) {}
    virtual ~nend_element_t() {}

    virtual void visit(stream_element_visitor_t &v) { v(*this); }
    virtual std::string str() const {
        return "NEnd(" + pos_or_not() + ")";
    }
};

class gbeg_element_t : public stream_element_t {
public:
    gbeg_element_t() : stream_element_t() {}
    gbeg_element_t(size_t hpos) : stream_element_t(hpos) {}
    virtual ~gbeg_element_t() {}

    virtual void visit(stream_element_visitor_t &v) { v(*this); }
    virtual std::string str() const {
        return "GBeg(" + pos_or_not() + ")";
    }
};

class gend_element_t : public stream_element_t {
public:
    gend_element_t() : stream_element_t() {}
    gend_element_t(size_t hpos) : stream_element_t(hpos) {}
    virtual ~gend_element_t() {}

    virtual void visit(stream_element_visitor_t &v) { v(*this); }
    virtual std::string str() const {
        return "GEnd(" + pos_or_not() + ")";
    }
};

// Once we have the stream, we can begin massaging it prior to pretty
// printing.  C++ native streams aren't really suitable for us; we
// have too much internal state.  Fortunately chain calling functions
// can work, so we set up some machinery to help with that.
class fn_wrapper_t : public single_threaded_countable_t<fn_wrapper_t> {
    counted_t<stream_element_visitor_t> v;
    std::string name;

public:
    fn_wrapper_t(counted_t<stream_element_visitor_t> _v, std::string _name)
        : v(_v), name(_name) {}

    void operator()(stream_handle_t e) {
        e->visit(*v);
    }
};

typedef counted_t<fn_wrapper_t> thunk_t;

// The first phase is to just generate the stream elements from the
// document tree, which is simple enough.
class generate_stream_visitor_t : public document_visitor_t {
    thunk_t fn;
public:
    generate_stream_visitor_t(thunk_t f) : fn(f) {}
    virtual ~generate_stream_visitor_t() {}

    virtual void operator()(const text_t &t) const {
        (*fn)(make_counted<text_element_t>(t.text));
    }

    virtual void operator()(const cond_t &c) const {
        (*fn)(make_counted<cond_element_t>(c.small, c.tail, c.cont));
    }

    virtual void operator()(const concat_t &c) const {
        std::for_each(c.children.begin(), c.children.end(),
                      [this](doc_handle_t d) { d->visit(*this); });
    }

    virtual void operator()(const group_t &g) const {
        (*fn)(make_counted<gbeg_element_t>());
        g.child->visit(*this);
        (*fn)(make_counted<gend_element_t>());
    }

    virtual void operator()(const nest_t &n) const {
        (*fn)(make_counted<nbeg_element_t>());
        (*fn)(make_counted<gbeg_element_t>());
        n.child->visit(*this);
        (*fn)(make_counted<gend_element_t>());
        (*fn)(make_counted<nend_element_t>());
    }
};

void generate_stream(doc_handle_t doc, thunk_t fn) {
    generate_stream_visitor_t v(std::move(fn));
    doc->visit(v);
}

// The second phase is to annotate the stream elements with the
// horizontal position of their last character (assuming no line
// breaks).  We can't actually do this successfully for
// `nbeg_element_t` and `gbeg_element_t` at this time, but everything
// else is pretty easy.
class annotate_stream_visitor_t : public stream_element_visitor_t {
    thunk_t fn;
    unsigned int position;
public:
    annotate_stream_visitor_t(thunk_t f) : fn(f), position(0) {}
    virtual ~annotate_stream_visitor_t() {}

    virtual void operator()(text_element_t &t) {
        position += t.payload.size();
        t.hpos = position;
        (*fn)(t.counted_from_this());
    }

    virtual void operator()(cond_element_t &c) {
        position += c.small.size();
        c.hpos = position;
        (*fn)(c.counted_from_this());
    }

    virtual void operator()(gbeg_element_t &e) {
        // can't do this accurately
        (*fn)(e.counted_from_this());
    }

    virtual void operator()(gend_element_t &e) {
        e.hpos = position;
        (*fn)(e.counted_from_this());
    }

    virtual void operator()(nbeg_element_t &e) {
        // can't do this accurately
        (*fn)(e.counted_from_this());
    }

    virtual void operator()(nend_element_t &e) {
        e.hpos = position;
        (*fn)(e.counted_from_this());
    }
};

thunk_t annotate_stream(thunk_t fn) {
    return make_counted<fn_wrapper_t>(
        make_counted<annotate_stream_visitor_t>(fn),
        "annotate");
}

// The third phase is to accurately compute the `hpos` for
// `gbeg_element_t`.  We don't care about the hpos for
// `nbeg_element_t`, but the `gbeg_element_t` is important for line
// breaking.  We couldn't accurately annotate it
// `annotate_stream_visitor_t`; this corrects that oversight.
class correct_gbeg_visitor_t : public stream_element_visitor_t {
    thunk_t fn;
    typedef std::unique_ptr<std::list<stream_handle_t> > buffer_t;
    std::vector<buffer_t> lookahead;

public:
    correct_gbeg_visitor_t(thunk_t f) : fn(f), lookahead() {}
    virtual ~correct_gbeg_visitor_t() {}

    void maybe_push(stream_element_t &e) {
        if (lookahead.empty()) {
            (*fn)(e.counted_from_this());
        } else {
            lookahead.back()->push_back(e.counted_from_this());
        }
    }

    virtual void operator()(text_element_t &t) {
        guarantee(t.hpos);
        maybe_push(t);
    }

    virtual void operator()(cond_element_t &c) {
        guarantee(c.hpos);
        maybe_push(c);
    }

    virtual void operator()(nbeg_element_t &e) {
        guarantee(!e.hpos);     // don't care about `nbeg_element_t` hpos
        maybe_push(e);
    }

    virtual void operator()(nend_element_t &e) {
        guarantee(e.hpos);
        maybe_push(e);
    }

    virtual void operator()(gbeg_element_t &e) {
        guarantee(!e.hpos);     // `hpos` shouldn't be set for `gbeg_element_t`
        lookahead.push_back(buffer_t(new std::list<stream_handle_t>()));
    }

    virtual void operator()(gend_element_t &e) {
        guarantee(e.hpos);
        buffer_t b(std::move(lookahead.back()));
        lookahead.pop_back();
        if (lookahead.empty()) {
            // this is then the topmost group
            (*fn)(make_counted<gbeg_element_t>(*(e.hpos)));
            for (const auto &element : *b) (*fn)(element);
            (*fn)(e.counted_from_this());
        } else {
            buffer_t &b2 = lookahead.back();
            b2->push_back(make_counted<gbeg_element_t>(*(e.hpos)));
            b2->splice(b2->end(), *b);
            b2->push_back(e.counted_from_this());
        }
    }
};

thunk_t correct_gbeg_stream(thunk_t fn) {
    return make_counted<fn_wrapper_t>(
        make_counted<correct_gbeg_visitor_t>(fn),
        "correct_gbeg");
}

// Kiselyov's original formulation includes an alternate third phase
// which limits lookahead to the width of the page.  This is difficult
// for us because we don't guarantee docs are of nonzero length,
// although that could be finessed, and also it adds extra complexity
// for minimal benefit, so skip it.

// The final phase is to compute output.  Each time we see a
// `gbeg_element_t`, we can compare its `hpos` with `rightEdge` to see
// whether it'll fit without breaking.  If it does fit, increment
// `fittingElements` and proceed, which will cause the logic for
// `text_element_t` and `cond_element_t` to just append stuff without
// line breaks.  If it doesn't fit, set `fittingElements` to 0, which
// will cause `cond_element_t` to do line breaks.  When we do a line
// break, we need to compute where the new right edge of the 'page'
// would be in the context of the original stream; so if we saw a
// `cond_element_t` with `e.hpos` of 300 (meaning it ends at
// horizontal position 300), the new right edge would be 300 -
// indentation + page width.
//
// `output_visitor_t` outputs to a string which is used as an append
// buffer; it could, in theory, stream the output but this isn't
// useful at present.
class output_visitor_t : public stream_element_visitor_t {
    const unsigned int width;
    unsigned int fittingElements, rightEdge, hpos;
    std::vector<unsigned int> indent;
public:
    std::string result;
    output_visitor_t(unsigned int w)
        : width(w), fittingElements(0), rightEdge(w), hpos(0), indent(),
          result() {}
    virtual ~output_visitor_t() {}

    virtual void operator()(text_element_t &t) {
        result += t.payload;
        hpos += t.payload.size();
    }

    virtual void operator()(cond_element_t &c) {
        if (fittingElements == 0) {
            unsigned int currentIndent = indent.empty() ? 0 : indent.back();
            result += c.tail;
            result += '\n';
            result += std::string(currentIndent, ' ');
            result += c.cont;
            fittingElements = 0;
            hpos = currentIndent + c.cont.size();
            rightEdge = (width - hpos) + *(c.hpos);
        } else {
            result += c.small;
            hpos += c.small.size();
        }
    }

    virtual void operator()(gbeg_element_t &e) {
        if (fittingElements != 0 ||
            static_cast<unsigned int>(*(e.hpos)) <= rightEdge) {
            ++fittingElements;
        } else {
            fittingElements = 0;
        }
    }

    virtual void operator()(gend_element_t &) {
        if (fittingElements != 0) {
            --fittingElements;
        }
    }

    virtual void operator()(nbeg_element_t &) {
        indent.push_back(hpos);
    }

    virtual void operator()(nend_element_t &) { indent.pop_back(); }
};

// Here we assemble the chain whose elements we have previously forged.
std::string pretty_print(unsigned int width, doc_handle_t doc) {
    counted_t<output_visitor_t> output =
        make_counted<output_visitor_t>(width);
    thunk_t corr_gbeg =
        correct_gbeg_stream(make_counted<fn_wrapper_t>(output, "output"));
    thunk_t annotate = annotate_stream(std::move(corr_gbeg));
    generate_stream(doc, std::move(annotate));
    return output->result;
}

} // namespace pprint

/*++
Copyright (c) 2024 Microsoft Corporation

Module Name:

    sls_datatype_plugin.cpp

Abstract:

    Algebraic Datatypes for SLS
    
Author:

    Nikolaj Bjorner (nbjorner) 2024-10-14

Notes:

Eager reduction to EUF:
   is-c(c(t))                for each c(t) in T
   acc_i(c(t_i)) = t_i       for each c(..t_i..) in T
   is-c(t) => t = c(...acc_j(t)..) for each acc_j(t) in T

   sum_i is-c_i(t) = 1
   is-c(t) <=> c = t         for each 0-ary constructor c

   is-c(t) <=> t = c(acc_1(t)..acc_n(t))

   s = acc(...(acc(t)) => s != t   if t is recursive

   or_i t = t_i              if t is a finite sort with terms t_i


   s := acc(t)                  => s < t in P
   a := s = acc(t), a is a unit => s < t in P
   a := s = acc(t), a in Atoms  => (a => s < t) in P

   s << t if there is a path P with conditions L.
   L => s != t   

   This disregards if acc is applied to non-matching constructor.
   In this case we rely on that the interpretation of acc can be
   forced.
   If this is incorrect, include is-c(t) assumptions in path axioms.

   Is P sufficient? Should we just consider all possible paths of depth at most k to be safe?
   Example: 
      C(acc(t)) == C(s)
   triggers equation acc(t) = s, but the equation is implicit, so acc(t) and s are not directly
   connected.
   Even, the axioms extracted from P don't consider transitivity of =.
   So the can-be-equal alias approximation is too strong.
   We therefore add an occurs check during propagation and lazily add missed axioms.


Model-repair based:

1. Initialize uninterpreted datatype nodes to hold arbitrary values.
2. Initialize datatype nodes by induced evaluation.
3. Atomic constraints are of the form for datatype terms
   x = y, x = t, x != y, x != t; s = t, s != t

   violated x = y:  x <- eval(y), y <- eval(x) or x, y <- fresh
   violated x = t:  x <- eval(t), repair t using the shape of x
   violated x != y: x <- fresh, y <- fresh
   violated x != t: x <- fresh, subterm y of t: y <- fresh

   acc(x) = t: eval(x) = c(u, v) acc(c(u,v)) = u -> repair(u = t)
   acc(x) = t: eval(x) does not match acc        -> acc(x) 
   has a fixed interpretation, so repair over t instead, or update interpretation of x

   uses:
    model::get_fresh_value(s)
    model::get_some_value(s)

--*/

#include "ast/sls/sls_datatype_plugin.h"
#include "ast/sls/sls_euf_plugin.h"
#include "ast/ast_pp.h"

namespace sls {
    
    datatype_plugin::datatype_plugin(context& c):
        plugin(c),
        euf(c.euf()),
        g(c.egraph()),
        dt(m),
        m_axioms(m),
        m_values(m) {
        m_fid = dt.get_family_id();
    }
    
    datatype_plugin::~datatype_plugin() {}

    void datatype_plugin::collect_path_axioms() {
        expr* t = nullptr, *z = nullptr;
        for (auto s : ctx.subterms()) {
            if (dt.is_accessor(s, t) && dt.is_recursive(t) && dt.is_recursive(s))
                add_edge(s, t, m.mk_app(dt.get_constructor_is(dt.get_accessor_constructor(to_app(s)->get_decl())), t));
            if (dt.is_constructor(s) && dt.is_recursive(s)) {
                for (auto arg : *to_app(s))
                    add_edge(arg, s, nullptr);
            }
        }
        expr* x = nullptr, *y = nullptr;
        for (sat::bool_var v = 0; v < ctx.num_bool_vars(); ++v) {
            expr* e = ctx.atom(v);
            if (!e)
                continue;
            if (!m.is_eq(e, x, y))
                continue;
            if (!dt.is_recursive(x))
                continue;
            sat::literal lp(v, false), ln(v, true);
            if (dt.is_accessor(x, z) && dt.is_recursive(z)) {
                if (ctx.is_unit(lp))
                    add_edge(y, z, nullptr);
                else if (ctx.is_unit(ln))
                    ;
                else
                    add_edge(y, z, e);                    
            }
            if (dt.is_accessor(y, z) && dt.is_recursive(z)) {
                if (ctx.is_unit(lp))
                    add_edge(x, z, m.mk_app(dt.get_constructor_is(dt.get_accessor_constructor(to_app(y)->get_decl())), z));
                else if (ctx.is_unit(ln))
                    ;
                else
                    add_edge(x, z, e);                    
            }            
        }
        add_path_axioms();
    }

    void datatype_plugin::add_edge(expr* child, expr* parent, expr* cond) {
        m_parents.insert_if_not_there(child, vector<parent_t>()).push_back({parent, expr_ref(cond, m)});
        TRACE("dt", tout << mk_bounded_pp(child, m) << " <- " << mk_bounded_pp(parent, m) << " " << mk_bounded_pp(cond, m) << "\n");
    }

    void datatype_plugin::add_path_axioms() {
        ptr_vector<expr> path;
        sat::literal_vector lits;
        for (auto [child, parents] : m_parents) {
            path.reset();
            lits.reset();
            path.push_back(child);
            add_path_axioms(path, lits, parents);
        }
    }

    void datatype_plugin::add_path_axioms(ptr_vector<expr>& children, sat::literal_vector& lits, vector<parent_t> const& parents) {
        for (auto const& [parent, cond] : parents) {
            if (cond)                    
                lits.push_back(~ctx.mk_literal(cond));
            if (children.contains(parent)) {
                // only assert loop clauses for proper loops
                if (parent == children[0])
                    ctx.add_clause(lits);
                if (cond)                    
                    lits.pop_back();
                continue;
            }
            if (children[0]->get_sort() == parent->get_sort()) {
                lits.push_back(~ctx.mk_literal(m.mk_eq(children[0], parent)));
                TRACE("dt", for (auto lit : lits) tout << (lit.sign() ? "~": "") << mk_pp(ctx.atom(lit.var()), m) << "\n";);
                ctx.add_clause(lits);
                lits.pop_back();
            }
            auto child = children.back();
            if (m_parents.contains(child)) {
                children.push_back(parent);
                auto& parents2 = m_parents[child];
                add_path_axioms(children, lits, parents2);
                children.pop_back();                
            }
            if (cond)                    
                lits.pop_back();
        }
    }

    void datatype_plugin::add_axioms() {
        expr_ref_vector axioms(m);
        expr* u = nullptr;
        for (auto t : ctx.subterms()) {
            auto s = t->get_sort();
            if (dt.is_datatype(s)) 
                m_dts.insert_if_not_there(s, ptr_vector<expr>()).push_back(t);

            if (!is_app(t))
                continue;
            auto ta = to_app(t);
            auto f = ta->get_decl();
            
            if (dt.is_constructor(t)) {
                auto r = dt.get_constructor_is(f);
                m_axioms.push_back(m.mk_app(r, t));
                auto& acc = *dt.get_constructor_accessors(f);
                for (unsigned i = 0; i < ta->get_num_args(); ++i) {
                    auto ti = ta->get_arg(i);
                    m_axioms.push_back(m.mk_eq(ti, m.mk_app(acc[i], t)));
                }
                auto& cns = *dt.get_datatype_constructors(s);
                for (auto c : cns) {
                    if (c != f) {
                        auto r2 = dt.get_constructor_is(c);
                        m_axioms.push_back(m.mk_not(m.mk_app(r2, t)));
                    }
                }
                continue;
            }

            if (dt.is_recognizer0(f)) {
                auto u = ta->get_arg(0);
                auto c = dt.get_recognizer_constructor(f);
                m_axioms.push_back(m.mk_iff(t, m.mk_app(dt.get_constructor_is(c), u)));
            }
            
            if (dt.is_update_field(t)) {
                NOT_IMPLEMENTED_YET();
            }

            if (dt.is_datatype(s)) {
                auto& cns = *dt.get_datatype_constructors(s);
                expr_ref_vector ors(m);
                for (auto c : cns) {
                    auto r = dt.get_constructor_is(c);
                    ors.push_back(m.mk_app(r, t));
                }
                m_axioms.push_back(m.mk_or(ors));
                for (unsigned i = 0; i < cns.size(); ++i) {
                    auto r1 = dt.get_constructor_is(cns[i]);
                    for (unsigned j = i + 1; j < cns.size(); ++j) {
                        auto r2 = dt.get_constructor_is(cns[j]);
                        m_axioms.push_back(m.mk_or(m.mk_not(m.mk_app(r1, t)), m.mk_not(m.mk_app(r2, t))));
                    }
                }
                for (auto c : cns) {
                    auto r = dt.get_constructor_is(c);
                    auto& acc = *dt.get_constructor_accessors(c);
                    expr_ref_vector args(m);
                    for (auto a : acc)
                        args.push_back(m.mk_app(a, t));
                    m_axioms.push_back(m.mk_iff(m.mk_app(r, t), m.mk_eq(t, m.mk_app(c, args))));
                }
            }
        }
        collect_path_axioms();

        TRACE("dt", for (auto a : m_axioms) tout << mk_pp(a, m) << "\n";);

        for (auto a : m_axioms)
            ctx.add_constraint(a);
    }

    void datatype_plugin::initialize() {
        add_axioms();
    }

    expr_ref datatype_plugin::get_value(expr* e) {
        if (!dt.is_datatype(e))
            return expr_ref(m);
        init_values();
        return expr_ref(m_values.get(g->find(e)->get_root_id()), m);
    }

    void datatype_plugin::init_values() {
        if (!m_values.empty())
            return;
        TRACE("dt", g->display(tout));
        m_model = alloc(model, m);
        // retrieve e-graph from sls_euf_solver: add bridge in sls_context to share e-graph
        SASSERT(g);
        // build top_sort<euf::enode> similar to dt_solver.cpp
        top_sort<euf::enode> deps;
        for (auto* n : g->nodes())
            if (n->is_root())
                add_dep(n, deps);

        auto trace_assignment = [&](euf::enode* n) {
            for (auto sib : euf::enode_class(n))
                tout << g->bpp(sib) << " ";
            tout << " <- " << mk_bounded_pp(m_values.get(n->get_id()), m) << "\n";
        };
        deps.topological_sort();
        expr_ref_vector args(m);
        euf::enode_vector leaves, worklist;
        obj_map<euf::enode, euf::enode_vector> leaf2root;
        // walk topological sort in order of leaves to roots, attaching values to nodes.
        for (euf::enode* n : deps.top_sorted()) {
            SASSERT(n->is_root());
            unsigned id = n->get_id();
            if (m_values.get(id, nullptr))
                continue;
            expr* e = n->get_expr();
            m_values.reserve(id + 1);
            if (!dt.is_datatype(e))
                continue;
            euf::enode* con = get_constructor(n);
            if (!con) {
                leaves.push_back(n);
                continue;
            }
            auto f = con->get_decl();
            args.reset();
            bool has_null = false;
            for (auto arg : euf::enode_args(con)) {
                if (dt.is_datatype(arg->get_sort())) {
                    auto val_arg = m_values.get(arg->get_root_id());
                    if (!val_arg)
                        has_null = true;
                    leaf2root.insert_if_not_there(arg->get_root(), euf::enode_vector()).push_back(n);
                    args.push_back(val_arg);
                }
                else
                    args.push_back(ctx.get_value(arg->get_expr()));
            }
            if (!has_null) {                
                m_values.setx(id, m.mk_app(f, args));
                m_model->register_value(m_values.get(id));
                TRACE("dt", tout << "Set interpretation "; trace_assignment(n););
            }
        }

        TRACE("dt",
            for (euf::enode* n : deps.top_sorted()) {
                tout << g->bpp(n) << ": ";
                tout << g->bpp(get_constructor(n)) << " :: ";
                auto s = deps.get_dep(n);
                if (s) {
                    tout << " -> ";
                    for (auto t : *s)
                        tout << g->bpp(t) << " ";
                }
                tout << "\n";
            }
        );

        // attach fresh values to each leaf, walk up parents to assign them values.
        while (!leaves.empty()) {
            SASSERT(worklist.empty());
            auto n = leaves.back();            
            leaves.pop_back();
            SASSERT(!get_constructor(n));
            auto v = m_model->get_fresh_value(n->get_sort());
            if (!v)
                v = m_model->get_some_value(n->get_sort());
            SASSERT(v);
            unsigned id = n->get_id();
            m_values.setx(id, v);
            TRACE("dt", tout << "Fresh interpretation "; trace_assignment(n););
            worklist.push_back(n);

            while (!worklist.empty()) {
                n = worklist.back();
                worklist.pop_back();
                if (!leaf2root.contains(n))
                    continue;
                for (auto p : leaf2root[n]) {
                    auto con = get_constructor(p);
                    SASSERT(con);
                    auto f = con->get_decl();
                    args.reset();
                    bool has_missing = false;
                    for (auto arg : euf::enode_args(con)) {
                        if (dt.is_datatype(arg->get_sort())) {
                            auto arg_val = m_values.get(arg->get_root_id());
                            if (!arg_val)
                                has_missing = true;
                            args.push_back(arg_val);
                        }
                        else
                            args.push_back(ctx.get_value(arg->get_expr()));
                    }
                    if (has_missing)
                        continue;
                    worklist.push_back(p);
                    SASSERT(all_of(args, [&](expr* e) { return e != nullptr; }));
                    m_values.setx(p->get_id(), m.mk_app(f, args));
                    TRACE("dt", tout << "Patched interpretation "; trace_assignment(p););
                    m_model->register_value(m_values.get(p->get_id()));
                }
            }
        }
    }
    
    void datatype_plugin::add_dep(euf::enode* n, top_sort<euf::enode>& dep) {
        if (!dt.is_datatype(n->get_expr()))
            return;
        euf::enode* con = get_constructor(n);
        TRACE("dt", tout << g->bpp(n) << " con: " << g->bpp(con) << "\n";);
        if (!con)
            dep.insert(n, nullptr);
        else if (con->num_args() == 0)
            dep.insert(n, nullptr);
        else 
            for (euf::enode* arg : euf::enode_args(con))
                dep.add(n, arg->get_root());       
    }

    
    void datatype_plugin::start_propagation() {
        m_values.reset();
        m_model = nullptr;
    }

    euf::enode* datatype_plugin::get_constructor(euf::enode* n) const {
        euf::enode* con = nullptr;
        for (auto sib : euf::enode_class(n))
            if (dt.is_constructor(sib->get_expr()))
                return sib;
        return nullptr;
    }

    bool datatype_plugin::propagate() {       
        enum color_t { white, grey, black };
        svector<color_t> color;
        ptr_vector<euf::enode> stack;
        obj_map<sort, ptr_vector<expr>> sorts;

        auto set_conflict = [&](euf::enode* n) {
            expr_ref_vector diseqs(m);
            while (true) {
                auto n2 = stack.back();
                auto con2 = get_constructor(n2);
                if (n2 != con2)
                    diseqs.push_back(m.mk_not(m.mk_eq(n2->get_expr(), con2->get_expr())));
                if (n2->get_root() == n->get_root()) {
                    if (n != n2)
                        diseqs.push_back(m.mk_not(m.mk_eq(n->get_expr(), n2->get_expr())));
                    break;
                }
                stack.pop_back();
            }
            IF_VERBOSE(1, verbose_stream() << "cycle\n"; for (auto e : diseqs) verbose_stream() << mk_pp(e, m) << "\n";);
            ctx.add_constraint(m.mk_or(diseqs));
            ++m_stats.m_num_occurs;
        };

        for (auto n : g->nodes()) {
            if (!n->is_root())
                continue;
            expr* e = n->get_expr();
            if (!dt.is_datatype(e))
                continue;
            if (!ctx.is_relevant(e))
                continue;
            sort* s = e->get_sort();
            sorts.insert_if_not_there(s, ptr_vector<expr>()).push_back(e);

            auto c = color.get(e->get_id(), white);
            SASSERT(c != grey);
            if (c == black)
                continue;

            // dfs traversal of enodes, starting with n, 
            // with outgoing edges the arguments of con, where con
            // is a node in the same congruence class as n that is a constructor.
            // For every cycle accumulate a conflict.

            stack.push_back(n);
            while (!stack.empty()) {
                n = stack.back();
                unsigned id = n->get_root_id();
                c = color.get(id, white);
                euf::enode* con;

                switch (c) {
                case black:
                    stack.pop_back();
                    break;
                case grey:
                case white: 
                    color.setx(id, grey, white);
                    con = get_constructor(n);
                    if (!con)
                        goto done_with_node;
                    for (auto child : euf::enode_args(con)) {
                        auto c2 = color.get(child->get_root_id(), white);
                        switch (c2) {
                        case black:
                            break;
                        case grey:
                            set_conflict(child);
                            return true;
                        case white:
                            stack.push_back(child);
                            goto node_pushed;
                        }
                    }
                done_with_node:
                    color[id] = black;
                    stack.pop_back();
                node_pushed:
                    break;                
                }
            }                    
        }


        for (auto const& [s, elems] : sorts) {
            auto sz = s->get_num_elements();

            if (!sz.is_finite() || sz.size() >= elems.size())
                continue;
            ctx.add_constraint(m.mk_not(m.mk_distinct((unsigned)sz.size() + 1, elems.data())));                       
        }

        return false;
    }

    bool datatype_plugin::include_func_interp(func_decl* f) const {
        if (!dt.is_accessor(f))
            return false;
        func_decl* con_decl = dt.get_accessor_constructor(f);
        for (euf::enode* app : g->enodes_of(f)) {   
            euf::enode* con = get_constructor(app->get_arg(0));
            if (con && con->get_decl() != con_decl) 
                return true;
        }
        return false; 
    }

    std::ostream& datatype_plugin::display(std::ostream& out) const {
        for (auto a : m_axioms)
            out << mk_bounded_pp(a, m, 3) << "\n";
        return out;
    }
    
    void datatype_plugin::propagate_literal(sat::literal lit) {
        euf.propagate_literal(lit);
    }
   
    bool datatype_plugin::is_sat() { return true; }
    
    void datatype_plugin::register_term(expr* e) {}
        
    void datatype_plugin::collect_statistics(statistics& st) const {
        st.update("sls-dt-axioms", m_axioms.size());
        st.update("sls-dt-occurs-conflicts", m_stats.m_num_occurs);
    }
    
    void datatype_plugin::reset_statistics() {}
    
}
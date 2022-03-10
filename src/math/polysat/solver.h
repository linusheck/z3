/*++
Copyright (c) 2021 Microsoft Corporation

Module Name:

    polysat solver

Abstract:

    Polynomial solver for modular arithmetic.

Author:

    Nikolaj Bjorner (nbjorner) 2021-03-19
    Jakob Rath 2021-04-6

--*/
#pragma once

#include <limits>
#include "util/statistics.h"
#include "util/params.h"
#include "math/polysat/boolean.h"
#include "math/polysat/conflict.h"
#include "math/polysat/constraint.h"
#include "math/polysat/clause_builder.h"
#include "math/polysat/simplify.h"
#include "math/polysat/restart.h"
#include "math/polysat/explain.h"
#include "math/polysat/ule_constraint.h"
#include "math/polysat/justification.h"
#include "math/polysat/linear_solver.h"
#include "math/polysat/search_state.h"
#include "math/polysat/forbidden_intervals.h"
#include "math/polysat/trail.h"
#include "math/polysat/viable.h"
#include "math/polysat/log.h"



namespace polysat {

    class solver {

        struct stats {
            unsigned m_num_iterations;
            unsigned m_num_decisions;
            unsigned m_num_propagations;
            unsigned m_num_conflicts;
            unsigned m_num_bailouts;
            unsigned m_num_restarts;
            void reset() { memset(this, 0, sizeof(*this)); }
            stats() { reset(); }
        };

        friend class constraint;
        friend class ule_constraint;
        friend class mul_ovfl_constraint;
        friend class smul_fl_constraint;
        friend class op_constraint;
        friend class signed_constraint;
        friend class clause;
        friend class clause_builder;
        friend class conflict;
        friend class conflict_explainer;
        friend class simplify;
        friend class restart;
        friend class explainer;
        friend class inference_engine;
        friend class forbidden_intervals;
        friend class linear_solver;
        friend class viable;
        friend class search_state;
        friend class assignment_pp;
        friend class assignments_pp;
        friend class ex_polynomial_superposition;
        friend class inf_saturate;        
        friend class constraint_manager;
        friend class scoped_solverv;        
        friend class test_polysat;
        friend class test_fi;

        reslimit&                m_lim;
        params_ref               m_params;

        mutable scoped_ptr_vector<dd::pdd_manager> m_pdd;
        viable                   m_viable;   // viable sets per variable
        linear_solver            m_linear_solver;
        conflict                 m_conflict; 
        simplify                 m_simplify;
        restart                  m_restart;
        forbidden_intervals      m_forbidden_intervals;
        bool_var_manager         m_bvars;       // Map boolean variables to constraints
        var_queue                m_free_pvars;  // free poly vars
        stats                    m_stats;

        uint64_t                 m_max_conflicts = std::numeric_limits<uint64_t>::max();
        uint64_t                 m_max_decisions = std::numeric_limits<uint64_t>::max();

        // Per constraint state
        constraint_manager       m_constraints;

        // Per variable information
        vector<rational>         m_value;         // assigned value
        vector<justification>    m_justification; // justification for variable assignment
        vector<signed_constraints> m_pwatch;      // watch list datastructure into constraints.
#ifndef NDEBUG
        std::optional<pvar>      m_locked_wlist;  // restrict watch list modification while it is being propagated
        bool                     m_propagating = false;  // set to true during propagation
#endif
        ptr_vector<clause>       m_lemmas;

        unsigned_vector          m_activity; 
        vector<pdd>              m_vars;
        unsigned_vector          m_size;     // store size of variables.

        search_state             m_search;
        assignment_t const& assignment() const { return m_search.assignment(); }
        pdd subst(assignment_t const& sub, pdd const& p) const;

        unsigned                 m_qhead = 0; // next item to propagate (index into m_search)
        unsigned                 m_level = 0;

        svector<trail_instr_t>   m_trail;
        unsigned_vector          m_qhead_trail;
        unsigned_vector          m_cjust_trail;

        unsigned_vector          m_base_levels;  // External clients can push/pop scope. 

        void push_qhead() { 
            m_trail.push_back(trail_instr_t::qhead_i);
            m_qhead_trail.push_back(m_qhead);
        }

        void pop_qhead() {
            m_qhead = m_qhead_trail.back();
            m_qhead_trail.pop_back();
        }


        unsigned size(pvar v) const { return m_size[v]; }

        /**
         * undo trail operations for backtracking.
         * Each struct is a subclass of trail and implements undo().
         */

        void del_var();

        dd::pdd_manager& sz2pdd(unsigned sz) const;
        dd::pdd_manager& var2pdd(pvar v);

        void push_level();
        void pop_levels(unsigned num_levels);

        void assign_propagate(sat::literal lit, clause& reason);
        void assign_decision(sat::literal lit, clause& lemma);
        void assign_eval(sat::literal lit);
        void activate_constraint(signed_constraint c);
        void deactivate_constraint(signed_constraint c);
        unsigned level(sat::literal lit, clause const& cl);

        bool can_decide_on_lemma();
        void decide_on_lemma();
        void decide_on_lemma(clause& lemma);
        void enqueue_decision_on_lemma(clause& lemma);
        void drop_enqueued_lemma();

        void assign_core(pvar v, rational const& val, justification const& j);
        bool is_assigned(pvar v) const { return !m_justification[v].is_unassigned(); }
        bool is_decision(search_item const& item) const;

        bool should_search();

        void propagate(sat::literal lit);
        void propagate(pvar v);
        void propagate(pvar v, rational const& val, signed_constraint c);
        bool propagate(sat::literal lit, clause& cl);
        void erase_watch(pvar v, signed_constraint c);
        void erase_watch(signed_constraint c);
        void add_watch(signed_constraint c);
        void add_watch(signed_constraint c, pvar v);

        void set_conflict(signed_constraint c) { m_conflict.set(c); }
        void set_conflict(clause& cl) { m_conflict.set(cl); }
        void set_conflict(pvar v) { m_conflict.set(v); }

        bool can_decide() const { return !m_free_pvars.empty() || m_bvars.can_decide(); }
        void decide();
        void pdecide(pvar v);
        void bdecide(sat::bool_var b);

        void narrow(pvar v);
        void linear_propagate();


        bool is_conflict() const { return !m_conflict.empty(); }
        bool at_base_level() const;
        unsigned base_level() const;

        void resolve_conflict();
        void resolve_bool(sat::literal lit);
        void revert_decision(pvar v);
        void revert_bool_decision(sat::literal lit);

        // activity of variables based on standard VSIDS
        unsigned m_activity_inc = 128;
        unsigned m_variable_decay = 110;
        void inc_activity(pvar v);
        void decay_activity();
        void rescale_activity();

        void report_unsat();
        void learn_lemma(clause& lemma);
        void backjump(unsigned new_level);
        void add_clause(clause& lemma);
        void add_clause(signed_constraint c1, signed_constraint c2, bool is_redundant);
        void add_clause(signed_constraint c1, signed_constraint c2, signed_constraint c3, bool is_redundant);
        void add_clause(signed_constraint c1, signed_constraint c2, signed_constraint c3, signed_constraint c4, bool is_redundant);
        void add_clause(unsigned n, signed_constraint* cs, bool is_redundant);


        signed_constraint lit2cnstr(sat::literal lit) const { return m_constraints.lookup(lit); }
        static void insert_constraint(signed_constraints& cs, signed_constraint c);

        bool inc() { return m_lim.inc(); }

        bool invariant();
        static bool invariant(signed_constraints const& cs);
        bool wlist_invariant();
        bool assignment_invariant();
        bool verify_sat();
        
        bool can_propagate();
        void propagate();

    public:

        /**
         * to share chronology we pass an external trail stack.
         * every update to the solver is going to be retractable
         * by pushing an undo action on the trail stack.
         */
        solver(reslimit& lim);

        ~solver();

        /**
         * End-game satisfiability checker.
         *
         * Returns l_undef if the search cannot proceed.
         * Possible reasons:
         * - Resource limits are exhausted.
         */
        lbool check_sat();

        /**
         * retrieve unsat core dependencies
         */
        void unsat_core(dependency_vector& deps);
        
        /**
         * Add variable with bit-size. 
         */
        pvar add_var(unsigned sz);

        /**
         * Create polynomial terms
         */
        pdd var(pvar v) { return m_vars[v]; }

        /**
        * Create terms for unsigned quot-rem
        * 
        * Return tuple (quot, rem)
        * 
        * The following properties are enforced:
        * b*quot + rem = a
        * ~ovfl(b*quot)
        * rem < b or b = 0
        */
        std::tuple<pdd, pdd> quot_rem(pdd const& a, pdd const& b);

        /**
        * Create expression for the logical right shift of p by q.
        */
        pdd lshr(pdd const& p, pdd const& q);

        /**
         * Create expression for bit-wise and of p by q.
         */
        pdd band(pdd const& p, pdd const& q);

        /**
         * Create polynomial constant.
         */
        pdd value(rational const& v, unsigned sz);

        /**
         * Return value / level of v in the current model (only meaningful if check_sat() returned l_true).
         */
        rational get_value(pvar v) const { SASSERT(is_assigned(v)); return m_value[v]; }

        unsigned get_level(pvar v) const { SASSERT(is_assigned(v)); return m_justification[v].level(); }


        /**
         * Evaluate term under the current assignment.
         */
        bool try_eval(pdd const& p, rational& out_value) const;

        /**
         * Apply current substitution to p.
         */
        pdd subst(pdd const& p) const; 


        /** Create constraints */
        signed_constraint eq(pdd const& p) { return m_constraints.eq(p); }
        signed_constraint diseq(pdd const& p) { return ~m_constraints.eq(p); }
        signed_constraint eq(pdd const& p, pdd const& q) { return eq(p - q); }
        signed_constraint diseq(pdd const& p, pdd const& q) { return diseq(p - q); }
        signed_constraint eq(pdd const& p, rational const& q) { return eq(p - q); }
        signed_constraint eq(pdd const& p, unsigned q) { return eq(p - q); }
        signed_constraint diseq(pdd const& p, rational const& q) { return diseq(p - q); }
        signed_constraint diseq(pdd const& p, unsigned q) { return diseq(p - q); }
        signed_constraint ule(pdd const& p, pdd const& q) { return m_constraints.ule(p, q); }
        signed_constraint ule(pdd const& p, rational const& q) { return ule(p, p.manager().mk_val(q)); }
        signed_constraint ule(rational const& p, pdd const& q) { return ule(q.manager().mk_val(p), q); }
        signed_constraint ule(pdd const& p, int n) { return ule(p, rational(n)); }
        signed_constraint ule(int n, pdd const& p) { return ule(rational(n), p); }
        signed_constraint ult(pdd const& p, pdd const& q) { return m_constraints.ult(p, q); }
        signed_constraint ult(pdd const& p, rational const& q) { return ult(p, p.manager().mk_val(q)); }
        signed_constraint ult(rational const& p, pdd const& q) { return ult(q.manager().mk_val(p), q); }
        signed_constraint sle(pdd const& p, pdd const& q) { return m_constraints.sle(p, q); }
        signed_constraint slt(pdd const& p, pdd const& q) { return m_constraints.slt(p, q); }
        signed_constraint slt(pdd const& p, rational const& q) { return slt(p, p.manager().mk_val(q)); }
        signed_constraint slt(rational const& p, pdd const& q) { return slt(q.manager().mk_val(p), q); }
        signed_constraint slt(pdd const& p, int n) { return slt(p, rational(n)); }
        signed_constraint slt(int n, pdd const& p) { return slt(rational(n), p); }
        signed_constraint sgt(pdd const& p, pdd const& q) { return slt(q, p); }
        signed_constraint sgt(pdd const& p, int n) { return slt(n, p); }
        signed_constraint sgt(int n, pdd const& p) { return slt(p, n); }
        signed_constraint mul_ovfl(pdd const& p, pdd const& q) { return m_constraints.mul_ovfl(p, q); }
        signed_constraint mul_ovfl(rational const& p, pdd const& q) { return mul_ovfl(q.manager().mk_val(p), q); }
        signed_constraint smul_ovfl(pdd const& p, pdd const& q) { return m_constraints.smul_ovfl(p, q); }
        signed_constraint smul_udfl(pdd const& p, pdd const& q) { return m_constraints.smul_udfl(p, q); }
        signed_constraint bit(pdd const& p, unsigned i) { return m_constraints.bit(p, i); }

        /** Create and activate polynomial constraints. */
        void add_eq(pdd const& p, dependency dep = null_dependency)                         { assign_eh(eq(p), dep); }
        void add_eq(pdd const& p, pdd const& q, dependency dep = null_dependency)           { assign_eh(eq(p, q), dep); }
        void add_eq(pdd const& p, rational const& q, dependency dep = null_dependency)      { assign_eh(eq(p, q), dep); }
        void add_eq(pdd const& p, unsigned q, dependency dep = null_dependency)             { assign_eh(eq(p, q), dep); }
        void add_diseq(pdd const& p, dependency dep = null_dependency)                      { assign_eh(diseq(p), dep); }
        void add_diseq(pdd const& p, pdd const& q, dependency dep = null_dependency)        { assign_eh(diseq(p, q), dep); }
        void add_diseq(pdd const& p, rational const& q, dependency dep = null_dependency)   { assign_eh(diseq(p, q), dep); }
        void add_diseq(pdd const& p, unsigned q, dependency dep = null_dependency)          { assign_eh(diseq(p, q), dep); }
        void add_ule(pdd const& p, pdd const& q, dependency dep = null_dependency)          { assign_eh(ule(p, q), dep); }
        void add_ult(pdd const& p, pdd const& q, dependency dep = null_dependency)          { assign_eh(ult(p, q), dep); }
        void add_sle(pdd const& p, pdd const& q, dependency dep = null_dependency)          { assign_eh(sle(p, q), dep); }
        void add_slt(pdd const& p, pdd const& q, dependency dep = null_dependency)          { assign_eh(slt(p, q), dep); }
        void add_noovfl(pdd const& p, pdd const& q, dependency dep = null_dependency)       { assign_eh(~mul_ovfl(p, q), dep); }
        void add_ovfl(pdd const& p, pdd const& q, dependency dep = null_dependency)         { assign_eh(mul_ovfl(p, q), dep); }

        void add_ule(pdd const& p, rational const& q, dependency dep = null_dependency)     { add_ule(p, p.manager().mk_val(q), dep); }
        void add_ule(rational const& p, pdd const& q, dependency dep = null_dependency)     { add_ule(q.manager().mk_val(p), q, dep); }
        void add_ule(pdd const& p, unsigned q, dependency dep = null_dependency)            { add_ule(p, rational(q), dep); }
        void add_ule(unsigned p, pdd const& q, dependency dep = null_dependency)            { add_ule(rational(p), q, dep); }
        void add_ult(pdd const& p, rational const& q, dependency dep = null_dependency)     { add_ult(p, p.manager().mk_val(q), dep); }
        void add_ult(rational const& p, pdd const& q, dependency dep = null_dependency)     { add_ult(q.manager().mk_val(p), q, dep); }
        void add_ult(pdd const& p, unsigned q, dependency dep = null_dependency)            { add_ult(p, rational(q), dep); }
        void add_ult(unsigned p, pdd const& q, dependency dep = null_dependency)            { add_ult(rational(p), q, dep); }
        void add_noovfl(pdd const& p, rational const& q, dependency dep = null_dependency)  { add_noovfl(p, p.manager().mk_val(q), dep); }
        void add_noovfl(rational const& p, pdd const& q, dependency dep = null_dependency)  { add_noovfl(q, p, dep); }
        void add_noovfl(pdd const& p, unsigned q, dependency dep = null_dependency)         { add_noovfl(p, rational(q), dep); }
        void add_noovfl(unsigned p, pdd const& q, dependency dep = null_dependency)         { add_noovfl(q, p, dep); }

        /**
         * Activate the constraint corresponding to the given boolean variable.
         * Note: to deactivate, use push/pop.
         */
        void assign_eh(signed_constraint c, dependency dep);

        /**
         * Unit propagation accessible over API.
         */
        lbool unit_propagate();

        /**
         * External context managment.
         * Adds so-called user-scope.
         */
        void push();
        void pop(unsigned num_scopes = 1);
       
        std::ostream& display(std::ostream& out) const;

        void collect_statistics(statistics& st) const;

        params_ref const & params() const { return m_params;  }

        void updt_params(params_ref const& p);

    };  // class solver

    class assignments_pp {
        solver const& s;
    public:
        assignments_pp(solver const& s): s(s) {}
        std::ostream& display(std::ostream& out) const;
    };

    class assignment_pp {
        solver const& s;
        pvar var;
        rational const& val;
    public:
        assignment_pp(solver const& s, pvar var, rational const& val): s(s), var(var), val(val) {}
        std::ostream& display(std::ostream& out) const;
    };

    inline std::ostream& operator<<(std::ostream& out, solver const& s) { return s.display(out); }

    inline std::ostream& operator<<(std::ostream& out, assignment_pp const& p) { return p.display(out); }

    inline std::ostream& operator<<(std::ostream& out, assignments_pp const& a) { return a.display(out); }

}


#pragma once
#include <cassert>

#include "caf/all.hpp"

#include "patterns.hpp"
#include "utils/ns_type.hpp"
#include "utils/range.hpp"

using namespace caf;
using namespace std;

namespace caf_pp {
namespace pp_impl {

using namespace utils;

using up = atom_constant<caf::atom("up")>;
using down = atom_constant<caf::atom("down")>;

template <typename T> struct dac_state {
  int num_fragments;
  vector<T> partial_res;
};
template <typename C, typename I>
behavior dac_worker_fun(stateful_actor<dac_state<Range<I>>> *self,
                        DivConq<C> p_, actor parent_) {
  self->state.num_fragments = 0;

  // utility functions
  auto create_range = [=](ns_type<C> &ns_c, size_t start,
                          size_t end) -> Range<I> {
    assert(start >= 0 && end <= ns_c->size());
    I it_start = ns_c->begin() + start;
    I it_end = ns_c->begin() + end;
    return {it_start, it_end};
  };
  auto create_index = [=](ns_type<C> &ns_c,
                          Range<I> range) -> tuple<size_t, size_t> {
    size_t start = range.begin() - ns_c->begin();
    size_t end = range.end() - ns_c->begin();
    return {start, end};
  };
  auto send_parent_and_terminate = [=](ns_type<C> &ns_c, Range<I> &res) {
    tuple<size_t, size_t> t = create_index(ns_c, res);
    self->send(parent_, up::value, ns_c, size_t(get<0>(t)), size_t(get<1>(t)));
    self->quit();
  };
  return {[=](down, ns_type<C> &ns_c, size_t start, size_t end) {
            // divide
            auto &s = self->state;
            Range<I> op = create_range(ns_c, start, end);
            if (p_.cond_fun_(op)) {
              Range<I> res = p_.seq_fun_(op);
              send_parent_and_terminate(ns_c, res);
            } else {
              auto ops = p_.div_fun_(op);
              for (auto &op : ops) {
                auto a = self->spawn(dac_worker_fun<C, I>, p_,
                                     actor_cast<actor>(self));
                auto t = create_index(ns_c, op);
                self->send(a, down::value, ns_c, get<0>(t), get<1>(t));
                s.num_fragments += 1;
              }
            }
          },
          [=](up, ns_type<C> &ns_c, size_t start, size_t end) {
            // merge
            auto &s = self->state;
            s.num_fragments -= 1;
            auto res = create_range(ns_c, start, end);
            s.partial_res.push_back(move(res));
            if (s.num_fragments == 0) {
              Range<I> res = p_.merg_fun_(s.partial_res);
              send_parent_and_terminate(ns_c, res);
            }
          }};
}

struct dac_master_state {
  response_promise promis;
};
template <typename C, typename I>
behavior dac_master_fun(stateful_actor<dac_master_state> *self, DivConq<C> p_,
                        caf::optional<actor> out_) {
  return {[=](C &c) mutable {
            // divide
            ns_type<C> ns_c(move(c));

            auto a =
                self->spawn(dac_worker_fun<C, I>, p_, actor_cast<actor>(self));

            self->state.promis = self->make_response_promise();
            self->send(a, down::value, ns_c, size_t(0), ns_c->size());
            return self->state.promis;
          },
          [=](up, ns_type<C> ns_c, size_t, size_t) mutable {
            C c = ns_c.release();
            if (out_) {
              self->send(out_.value(), move(c));
              self->state.promis.deliver(0);
            } else {
              self->state.promis.deliver(move(c));
            }
          }};
}
} // namespace pp_impl
} // namespace caf_pp

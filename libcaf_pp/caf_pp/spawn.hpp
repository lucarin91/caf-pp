#pragma once

#include "caf/all.hpp"

#include "map.hpp"
#include "patterns.hpp"

using namespace caf;
using namespace std;

namespace caf_pp {

enum Runtime {
  threads,
  actors
};

template <class T>
caf::optional<actor> spawn_pattern(actor_system &sys, T &p,
                                   const caf::optional<actor> &out, Runtime m) {
  static_assert((is_base_of<Pattern, T>::value != 0),
                "Type parameter of this function must derive from Pattern");
  cout << "[DEBUG] "
       << "inside TOPFUN spawn" << endl;
  return {};
}

// SPAWN NODE
template <template <class> class P, typename T>
typename std::enable_if<std::is_same<P<T>, Seq<T>>::value,
                        caf::optional<actor>>::type
spawn_pattern(actor_system &sys, P<T> &p, const caf::optional<actor> &out,
              Runtime m) {
  cout << "[DEBUG] "
       << "inside NODE spawn" << endl;
  // actor a = apply([&](auto&&... args) {
  //   return sys.spawn<T>(out, std::forward<decltype(args)>(args)...);
  // }, p.args_);
  actor a;
  switch (m) {
  case Runtime::threads:
    a = sys.spawn<T, caf::detached>(out);
    break;
  case Runtime::actors:
    a = sys.spawn<T>(out);
    break;
  }
  if (p.spawn_cb_) {
    p.spawn_cb_.value()(a);
  }
  p.instance_ = caf::optional<actor>(a);
  return a;
}

// SPAWN FARM
template <template <class> class P, typename T>
typename std::enable_if<std::is_same<P<T>, Farm<T>>::value,
                        caf::optional<actor>>::type
spawn_pattern(actor_system &sys, P<T> &p, const caf::optional<actor> &out,
              Runtime m) {
  cout << "[DEBUG] "
       << "inside FARM spawn" << endl;
  auto spawn_fun = [&]() {
    cout << "[DEBUG] "
         << "inside actor_pool spawn_fun" << endl;
    return spawn_pattern(sys, p.stage_, out, m).value();
  };
  cout << "[DEBUG] "
       << "make actor_pool" << endl;
  auto a = caf::actor_pool::make(sys.dummy_execution_unit(), p.replicas_,
                                 spawn_fun, p.policy_);
  p.instance_ = caf::optional<actor>(a);
  return a;
}

// SPAWN MAP
template <template <typename> typename P, typename T>
typename std::enable_if<std::is_same<P<T>, Map<T>>::value,
                        caf::optional<actor>>::type
spawn_pattern(actor_system &sys, P<T> &p, const caf::optional<actor> &out,
              Runtime m) {
  cout << "[DEBUG] "
       << "inside Map spawn" << endl;
  using Iter = typename T::iterator;
  actor map;
  switch (p.sched_) {
  case PartitionSched::static_:
    map = sys.spawn(map_static_actor<T, Iter>, p.map_fun_, p.replicas_, out);
    break;
  case PartitionSched::dynamic_:
    map = sys.spawn(map_dynamic_actor<T, Iter>, p.map_fun_, p.replicas_, out);
    break;
  }
  p.instance_ = caf::optional<actor>(map);
  return map;
}

// SPAWN PIPELINE
template <template <class> class P, typename... T>
typename std::enable_if<std::is_same<P<T...>, Pipeline<T...>>::value,
                        caf::optional<actor>>::type
spawn_pattern(actor_system &sys, P<T...> &p, const caf::optional<actor> &out,
              Runtime m) {
  cout << "[DEBUG] "
       << "inside PIPELINE spawn" << endl;
  auto a = for_each_tuple(sys, p.stages_, out, m).value();
  p.instance_ = caf::optional<actor>(a);
  return a;
}

template <size_t I = 0, typename... Tp>
inline typename std::enable_if<I == sizeof...(Tp), caf::optional<actor>>::type
for_each_tuple(actor_system &sys, std::tuple<Tp...> &t,
               const caf::optional<actor> &out, Runtime m) {
  return out;
}

template <size_t I = 0, typename... Tp>
    inline typename std::enable_if <
    I<sizeof...(Tp), caf::optional<actor>>::type
    for_each_tuple(actor_system &sys, std::tuple<Tp...> &t,
                   const caf::optional<actor> &out, Runtime m) {
  auto last = spawn_pattern(sys, std::get<(sizeof...(Tp)) - 1 - I>(t), out, m);
  return for_each_tuple<I + 1, Tp...>(sys, t, caf::optional<actor>(last), m);
}

} // namespace caf_pp
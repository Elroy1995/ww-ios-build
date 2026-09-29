#ifndef BLUEWAKE_COMPOSITE_DISPATCH_LOOP_H
#define BLUEWAKE_COMPOSITE_DISPATCH_LOOP_H

#include "edge_intercept_abi.h"
#ifdef BLUEWAKE_EDGE_FILTER
#include "direct_calls.h"
#endif

typedef int (*BluewakeCompositeDispatchFn)(CPUState* ctx, u32 address);

/* The boundary loop, in the header and always inlined.
 *
 * Why it is not only the out-of-line function below. The composite dispatches
 * one guest block per iteration of this loop, and the dispatch arrives as a
 * function pointer, so passing it through the entry point of another
 * translation unit left the compiler with an indirect call it could not see
 * through: the pointer was reloaded from its stack slot at every guest edge and
 * neither selected_dispatch nor the chunk lookup beneath it could be inlined
 * into the loop. Called with a static function from the same translation unit
 * the call devirtualises, the lookup folds into the loop, and the loop's own
 * state survives an edge in registers instead of being rebuilt.
 *
 * The exported form and the unit test keep the out-of-line entry point; it is
 * this body, so the two cannot drift.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline int bluewake_chassis_dispatch_loop(
    CPUState* ctx, u32 address, BluewakeCompositeDispatchFn dispatch,
    BluewakeEdgeServiceFn edge_service, void* service_user) {
    if (ctx == NULL || dispatch == NULL)
        return 0;

    s64 prior_downcount = ctx->downcount;
    int dispatched = dispatch(ctx, address);
    if (!dispatched || edge_service == NULL)
        return dispatched;
    if (ctx->downcount >= prior_downcount)
        return 1;

    for (;;) {
        if (ctx->exception != 0u ||
            (ctx->cycle_budget > 0 &&
             ctx->downcount <= -ctx->cycle_budget))
            return 1;

        address = ctx->pc;
#ifdef BLUEWAKE_EDGE_FILTER
        /* The host's service lets this boundary pass: it is quiet and does not
         * watch this address (direct_calls.h). Otherwise it is asked. */
        if (!(bw_edge_filter_enabled && bw_host_quiet(ctx) && bw_edge_unwatched(address)))
#endif
        if (edge_service(service_user, ctx, address))
            return 1;

        prior_downcount = ctx->downcount;
        dispatched = dispatch(ctx, address);
        if (!dispatched)
            return 1;
        if (ctx->downcount >= prior_downcount) {
            /* A dispatched block that charges no cycles used to end the turn here,
             * and that one exit owns 43.6 percent of the boot's turns: 598,350 of
             * 1,372,978, with the whole inventory closing at budget 26.9, edge
             * service 29.4 and dispatcher-miss 0.12 (docs/status/CURRENT.md,
             * 2026-09-22). Tolerating a bounded run of them, screened on the
             * certified pair, halves the turns for +2.67 percent of the play
             * window - 398.3 M instructions a retrace against 409.2 M, digest
             * 92dd816c unchanged over 1,050 records, both ceilings stopping at
             * their certified pc.
             *
             * The bound is load-bearing rather than incidental: a block that
             * charges nothing does not advance downcount, so a guest that stops
             * charging - which is exactly what a stuck loop looks like - would
             * never reach the budget exit above. A run of nine ends the turn, so
             * the chassis always comes back to the host. */
#define BLUEWAKE_ZERO_CHARGE_RUN_MAX 8u
            static unsigned zero_charge_run;
            if (++zero_charge_run <= BLUEWAKE_ZERO_CHARGE_RUN_MAX)
                continue;
            zero_charge_run = 0u;
            return 1;
        }
    }
}

int bluewake_composite_dispatch_until_boundary(
    CPUState* ctx, u32 address, BluewakeCompositeDispatchFn dispatch,
    BluewakeEdgeServiceFn edge_service, void* service_user);

#endif

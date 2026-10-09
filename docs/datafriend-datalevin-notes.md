# Datalevin ideas for Datafriend

The subsequent [grouped-query implementation](datafriend-query-planner.md)
implements entity gathers, selective access paths and unused-binding elimination.
The recommendations below record the rationale for those changes.

Inspected Datalevin revision
[`a26e8dde72547dad594aced3ea714594aae0f6ec`](https://github.com/datalevin/datalevin/tree/a26e8dde72547dad594aced3ea714594aae0f6ec).
The checkout is under the ignored build directory; no upstream code was copied
or added as an application dependency.

## Useful ideas

| Datalevin mechanism | Application to Datafriend |
| --- | --- |
| Groups patterns by entity and gathers multiple attributes through one EAV scan. | Compile an entity-local query into a short sequence of field tests and projections, avoiding repeated generic variable binding. Our hash indexes require a different physical implementation from its ordered EAV cursor. |
| Uses index cardinalities to choose access paths and join order. | `AVE[attribute][value].size()` already gives an exact candidate count. Start full evaluations with the most selective constant clause, then follow bound entities. Preserve original projection indices when changing clause order. |
| Pushes filters into scans; removes unused value bindings while preserving existence tests. | Test predicates before fetching projection-only fields. Repeated value variables must retain equality checks. All current Datafriend attributes are single-valued, simplifying eligible cases. |
| Separates cached planning from attribute-dependent result caching. | Our compiled shared query groups already provide part of this. Cache query structure once, while deriving any data-sensitive starting-point choice from current index counts. |

Source locations: [entity grouping](https://github.com/datalevin/datalevin/blob/a26e8dde72547dad594aced3ea714594aae0f6ec/src/datalevin/query/optimizer/graph.clj#L405),
[merge-scan execution](https://github.com/datalevin/datalevin/blob/a26e8dde72547dad594aced3ea714594aae0f6ec/src/datalevin/query/plan.clj#L328),
[lookup costing](https://github.com/datalevin/datalevin/blob/a26e8dde72547dad594aced3ea714594aae0f6ec/src/datalevin/query/optimizer/estimates.clj#L106),
[query rewrites](https://github.com/datalevin/datalevin/blob/a26e8dde72547dad594aced3ea714594aae0f6ec/doc/query.md),
and [cache dependency analysis](https://github.com/datalevin/datalevin/blob/a26e8dde72547dad594aced3ea714594aae0f6ec/src/datalevin/query/cache.clj#L76).

## What to apply first

The current optimization answers **which entities need reevaluation**. It proves
that every clause uses the same entity variable or the same constant entity,
then restricts the existing matcher and result pruning to transaction-changed
entities. The task query therefore checks one entity after one task edit.

Datalevin's grouped entity gathering suggests the next step: make that small
evaluation cheaper through a compiled field-gather operation. Selective roots
would primarily improve initial subscriptions and full join reevaluation, where
we currently follow clause order. They would not remove a full-task-list scan
after every edit by themselves.

The inspected result-cache path extracts attribute dependencies and executes the
query on a cache miss. It is not evidence that Datalevin automatically maintains
our editable subscription maps incrementally. Keep incremental result maintenance
and query execution planning as separate concerns.

Do not copy the full cost-based planner yet. A small, measured entity-local path
and a selective initial scan fit our current queries. Hash joins, sampled cost
estimation and larger planning searches should follow demonstrated workloads.

Changed-entity reevaluation landed first. The subsequent planner implements
grouped field gathering, selective execution ordering and unused-binding
elimination; larger cost-based planning remains a possible future extension.

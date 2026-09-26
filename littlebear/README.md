# littlebear

A non-persistent memory-mapped alex set store.

---

## Motivation

littlebear is a cache layer for namespaced key-value pairs. The goals are:

- **Write throughput.** Namespaces are genuinly independent datastructures, writes across namespaces should have minimal contention.
- **Read throughput** Readers should never block.
- **Sync.** Any reader can perform a full efficient set reconciliation against any namespace at any time. 

littlebear uses a rank-partitioned augmented external zipzip tree rather than a B-tree:

- Concurrent reads are fully lock-free — no latch coupling.
- Range fingerprints are intrinsic to the tree structure; no separate index needed. the tree acts both as an index and a hashtree.
- No split or merge operations — the most complex part of B-tree implementations.
- btree matching cold page misses during lookup and insertion.
- fixed size nodes and simpler datastructure means we converge to full page utilization, i.e. minimal fragmentation.
- minimal overhead for namespace creation. One in memory hash table insert.

Littlebear has been designed as a backend for client facing applications. The intentions is that each users physical machine can be assigned one or several namespaces. Instead of streaming updates to the server, we always performe an efficient sync reconciliation algorithm between the client and server. This means that there are no special cases to handle: lag, disconnect and reconnect, client side log tracking. The client simply syncs to the server every time. If another user wants to see their friends data, they simply sync from their friends namespace. In contrast to typical approaches where developers implement complex queries to find the correct information, the complexity of littlebear comes from declaring good namespaces. I think this is much easier and natural to do. If a client has a todo list they want to sync, put all your todo list entities into a clienttodolist namespace and sync it to the server. Because your user ui generally has to partition data anyways to figure out what to show in what screen, relying on these paritions in the actual storage and backend layer should be easier than translating data to queries to data. You can think of littlebear as one big efficient nested map data structure where each top level key support efficient network syncyng.

```
{:namespace1 {:key1 val1 :key2 val2 :key3 val3...}}
 :namespace2 {:key1 val1 :key2 val2 :key3 val3...}}
 :namespace3 {:key1 val1 :key2 val2 :key3 val3...}}
 :namespace4 {:key1 val1 :key2 val2 :key3 val3...}}
 ...
```
creating namespaces is highly efficient. each namespace is an actual independent datastructure which means that write concurrency is optimal as long as namespaces are tied to physical single threaded processes which is often the case for many client server application, for example mobile apps. namespaces must be 32byte public keys, keys can be any 8byte sequence, and values can be arbitrary byte arrays. The bigger the value, the less efficient the set reconciliation. Our efficiency comes from figuring out missing keys, not from chunking everything. The api is minimal and behaves how you would expect a nested map to behave except with the added efficient syncyng protocol between namespaces.

## Design

Littlebear is still being actively worked on. Currently the design has moved from using a thread pool to a single threaded event loop system. The thread pool is nice in principle, it lets the os handle what seem to be most of the difficulties of concurrency with little downside. However, after working through the code, it seems like handing over concurrency control to the os is a mistake for this system. Because threads are always making progress, a thread pool design requires a concurrent datastructure. A zip tree can be modified to allow for concurrent readers with an exclusive writer which is exactly what we want, but, doing this comes at a cost of complexity and makes the code much harder to debug correctly in my experience. A single threaded design is easier to reason about, keeps data structures simpler, and in theory could be extended to provide the same guarantees as a scheduler system. In practice, the event loop can stay much simpler. Quick operations like tree lookups can occur entirely sequentially, faster, and with less overhead than with a thread pool if we start to use batching. Expensive operations like io are all asyncronous and also highly efficient because of batching. Overall, this makes the design considerably simpler and probably considerably more performant. 

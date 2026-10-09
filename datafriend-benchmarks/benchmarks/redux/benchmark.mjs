import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';
import { configureStore, createEntityAdapter, createSelector, createSlice } from '@reduxjs/toolkit';
import { legacy_createStore as createStore } from 'redux';
import { setAutoFreeze } from 'immer';

assert.equal(process.env.NODE_ENV, 'production');
const [implementation, mode, countText, samplesText, warmupText] = process.argv.slice(2);
const count = Number(countText), samples = Number(samplesText), warmup = Number(warmupText);
assert(['rtk', 'rtk_no_freeze', 'redux_manual'].includes(implementation));
assert(['edit', 'read', 'visible'].includes(mode));
assert([count, samples].every(x => Number.isInteger(x) && x > 0));
assert(Number.isInteger(warmup) && warmup >= 0);
setAutoFreeze(implementation === 'rtk');

// Same logical fixture as benchmark_redux.dart. Unchanged drawing and sticker
// references must survive a task edit; neither system copies their payloads.
const tasks = Array.from({ length: count }, (_, i) => ({
  kind: 'task', id: String(i), title: `task ${i}`, done: false, order: i,
}));
const stickers = [
  { id: 'my-list', asset: 'star.gif', label: 'My list', x: .18, y: .15 },
  { id: 'heart', asset: 'heart.gif', label: 'A little joy', x: .78, y: .43 },
  { id: 'penguin', asset: 'penguin.webp', label: 'One step at a time', x: .2, y: .76 },
];
const strokes = [{ id: 'drawing', page: 0, color: 0xff112233,
  points: Array.from({ length: 5000 }, (_, i) => ({ x: i / 5000, y: i })) }];
const adapter = createEntityAdapter({ sortComparer: (a, b) => a.order - b.order });
const initialTasks = adapter.setAll(adapter.getInitialState(), tasks);
const slice = createSlice({
  name: 'tasks', initialState: initialTasks,
  reducers: {
    toggle(state, action) {
      // 'done' cannot affect ordering, so the existing sorted ID array is valid.
      const task = state.entities[action.payload];
      task.done = !task.done;
    },
  },
});
const initial = { tasks: initialTasks, stickers, strokes };
const store = implementation === 'redux_manual'
  ? createStore((state = initial, action) => {
      if (action.type !== 'tasks/toggle') return state;
      const id = action.payload, task = state.tasks.entities[id];
      return { ...state, tasks: { ...state.tasks, entities: {
        ...state.tasks.entities, [id]: { ...task, done: !task.done },
      } } };
    })
  : configureStore({
      reducer: { tasks: slice.reducer, stickers: (s = stickers) => s, strokes: (s = strokes) => s },
      devTools: false,
    });
const selectors = adapter.getSelectors(state => state.tasks);
const completed = createSelector(
  [selectors.selectIds, selectors.selectEntities],
  (ids, entities) => {
    let n = 0;
    for (const id of ids) if (entities[id].done) n++;
    return n;
  },
);
// Three selector subscriptions, analogous to the three AppViews subscriptions.
// They only invalidate/notify here; requested UI rows are read after dispatch.
const changes = { tasks: 0, stickers: 0, strokes: 0 };
for (const key of Object.keys(changes)) {
  let previous = store.getState()[key];
  store.subscribe(() => {
    const current = store.getState()[key];
    if (current !== previous) { previous = current; changes[key]++; }
  });
}
let publications = 0;
store.subscribe(() => publications++);
const toggle = () => store.dispatch(slice.actions.toggle('0'));

// Verify immutable updates, ordering retention and unchanged-row sharing before
// measurement. Use real dispatches, including in the no-freeze configuration.
const before = store.getState();
toggle();
const after = store.getState();
assert.equal(before.tasks.entities['0'].done, false);
assert.equal(after.tasks.entities['0'].done, true);
assert.notEqual(after.tasks.entities, before.tasks.entities);
assert.equal(after.tasks.ids, before.tasks.ids);
if (count > 1) assert.equal(after.tasks.entities['1'], before.tasks.entities['1']);
assert.equal(after.strokes, before.strokes);
assert.equal(after.stickers, before.stickers);
assert.equal(Object.isFrozen(after.tasks), implementation === 'rtk');
toggle();
publications = changes.tasks = 0;

let checksum = 0, document, visibleDone = false, completedCount = 0;
function action() {
  toggle();
  const state = store.getState();
  if (mode === 'read') {
    document = { tasks: selectors.selectAll(state), stickers: state.stickers, strokes: state.strokes };
    for (const task of document.tasks) checksum += task.title.length + task.id.length + Number(task.done);
  } else if (mode === 'visible') {
    completedCount = completed(state);
    const ids = selectors.selectIds(state);
    for (let i = 0; i < Math.min(15, ids.length); i++) {
      const task = selectors.selectById(state, ids[i]);
      if (i === 0) visibleDone = task.done;
      checksum += task.title.length + task.id.length + Number(task.done);
    }
  }
}
const times = new Float64Array(samples);
for (let i = 0; i < warmup + samples; i++) {
  const start = performance.now();
  action();
  const elapsed = (performance.now() - start) * 1000;
  if (i >= warmup) times[i - warmup] = elapsed;
}
const iterations = warmup + samples, expectedDone = iterations % 2 === 1;
const final = store.getState();
assert.equal(publications, iterations);
assert.deepEqual(changes, { tasks: iterations, stickers: 0, strokes: 0 });
assert.equal(final.tasks.ids.length, count);
for (let i = 0; i < count; i++) {
  assert.equal(final.tasks.ids[i], String(i));
  assert.deepEqual(final.tasks.entities[String(i)], {
    kind: 'task', id: String(i), title: `task ${i}`, order: i, done: i === 0 && expectedDone,
  });
}
assert.equal(final.strokes, before.strokes);
assert.equal(final.stickers, before.stickers);
if (mode !== 'edit') {
  const rows = mode === 'read' ? count : Math.min(15, count);
  let base = 0;
  for (let i = 0; i < rows; i++) base += `task ${i}`.length + String(i).length;
  assert.equal(checksum, base * iterations + Math.ceil(iterations / 2));
  if (mode === 'read') assert.equal(document.tasks[0].done, expectedDone);
  else {
    assert.equal(visibleDone, expectedDone);
    assert.equal(completedCount, Number(expectedDone));
  }
}
const mean = times.reduce((sum, t) => sum + t, 0) / samples;
times.sort();
const at = percentile => times[Math.floor(samples * percentile)].toFixed(3);
console.log('workload,build,phase,kind,unit,samples,p50,p95,p99,max,mean');
console.log(`${mode}_${count},control,total,wall,us,${samples},${at(.5)},${at(.95)},${at(.99)},${times[samples - 1].toFixed(3)},${mean.toFixed(3)}`);

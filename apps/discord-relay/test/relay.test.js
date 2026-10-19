'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { toQueueRow, RateLimiter } = require('../src/relay');

const map = new Map([['111', 7]]);
const msg = (over = {}) => ({
  channelId: '111',
  cleanContent: 'hello',
  author: { id: '5', username: 'bob', bot: false },
  member: { displayName: 'Bobby' },
  ...over,
});

test('maps a message from a bound channel', () => {
  assert.deepStrictEqual(toQueueRow(msg(), map), {
    wowGuildId: 7,
    discordUserId: '5',
    displayName: 'Bobby',
    message: 'hello',
  });
});

test('falls back to username without a member', () => {
  assert.strictEqual(toQueueRow(msg({ member: null }), map).displayName, 'bob');
});

test('ignores unmapped channels, bots, webhooks and empty text', () => {
  assert.strictEqual(toQueueRow(msg({ channelId: '999' }), map), null);
  assert.strictEqual(toQueueRow(msg({ author: { id: '1', bot: true } }), map), null);
  assert.strictEqual(toQueueRow(msg({ webhookId: '42' }), map), null);
  assert.strictEqual(toQueueRow(msg({ cleanContent: '   ' }), map), null);
});

test('truncates long text', () => {
  assert.strictEqual(toQueueRow(msg({ cleanContent: 'x'.repeat(5000) }), map).message.length, 2000);
});

test('rate limiter allows the limit per window per key', () => {
  let t = 0;
  const rl = new RateLimiter(2, 1000, () => t);
  assert.ok(rl.allow('a'));
  assert.ok(rl.allow('a'));
  assert.ok(!rl.allow('a'));
  assert.ok(rl.allow('b'));
  t = 1001;
  assert.ok(rl.allow('a'));
});

local key = KEYS[1]
local capacity = tonumber(ARGV[1])
local rate = tonumber(ARGV[2])
local now_ms = tonumber(ARGV[3])
local key_ttl = tonumber(ARGV[4]) or 3600

local bucket = redis.call('HMGET', key, 'tokens', 'last_refill_ms')
local tokens = tonumber(bucket[1]) or capacity
local last_refill_ms = tonumber(bucket[2]) or now_ms

local elapsed_ms = math.max(0, now_ms - last_refill_ms)
local refill_tokens = elapsed_ms * rate / 1000

if refill_tokens > 0 then
    tokens = math.min(capacity, tokens + refill_tokens)
    last_refill_ms = now_ms
end

local allowed = 0;
if tokens >= 1 then
    tokens = tokens - 1
    allowed = 1
else
    allowed = 0
end

redis.call('HMSET', key, 'tokens', tokens, 'last_refill_ms', last_refill_ms)
redis.call('EXPIRE', key, key_ttl)

return allowed
# messenger

A lightweight, thread-safe, header-only event messaging system for C++17.
## Features

- 📦 **Header-only** - just include `messenger.hpp`
- 🛡️ **Type-safe** - messages are dispatched by type with no manual casting
- 🧵 **Thread-safe** - post messages from any thread for handling on another thread
- 🔢 **Priority ordering** - control the order handlers are invoked
- 🚫 **Exclusive handling** - handlers can exclusively handle a message, preventing it from reaching lower-priority subscribers
- ⚡ **Ephemeral/"Handle Once" subscriptions** - handlers that automatically remove themselves after firing once
- 🧩 **Customizable type IDs** - swap in your own event type identification strategy
- ⏱️ **Queued & immediate dispatch** - post to a queue for deferred processing or dispatch inline
- 🧰 **Zero dependencies** - standard library only, no allocators or frameworks required

## Requirements

- C++17 or later

## Getting Started

Copy `messenger.hpp` into your project and include it:

```cpp
#include "messenger.hpp"

using namespace aufority::msging;
```

## Usage

```cpp
#include "messenger.hpp"
#include <iostream>

using namespace aufority::msging;

struct UserJoinedEvent
{
    std::string name;
};

int main()
{
    Messenger messenger;
    SubscriptionHandle handle;

    messenger.Subscribe<UserJoinedEvent>(
        handle, 
        [](UserJoinedEvent& event)
        {
            printf("Hello, %s\n", event.name.c_str()));
        }
    );
    
    UserJoinedEvent event;
    event.name = "John";

    // Dispatch immediately on the calling thread
    messenger.Dispatch(event);

    return 0;
}
```

## Usage

### Subscribing

Subscribe to a message type by providing a handle and a handler. The handle manages the lifetime of the subscription, so keep it alive for as long as you want to receive events.
> [!NOTE]
> When a handle goes out of scope, its subscription is automatically removed.

```cpp
SubscriptionHandle handle;

messenger.Subscribe<MyEvent>(
    handle, 
    [](MyEvent& event)
    {
        // handle event
    }
);
```

### Dispatching

**Immediate dispatch** invokes all matching handlers synchronously on the calling thread:

> [!WARNING]
> Immediate dispatch is only intended for single-threaded use-cases, meaning this function is not thread-safe. Utilize the queued dispatch if you wish to dispatch messages from other threads.

```cpp
messenger.Dispatch(MyEvent());
```

**Queued dispatch** lets you post messages from any thread and process them later (e.g. on your main thread):

```cpp
// From any thread:
messenger.Post(MyEvent());

// On an update/tick thread or when you wish to dispatch messages:
messenger.DispatchQueued(); // dispatch all
messenger.DispatchQueued(10); // dispatch up to 10 messages at once
```

### Unsubscribing

Unsubscription happens automatically when the handle is destroyed. You can also unsubscribe explicitly:

```cpp
messenger.Unsubscribe(handle);
```

### Priority Ordering

Higher priority handlers run first. Default priority is `0`. If you are not using a priority, then order of handling is based on insertion order (FIFO).

```cpp
SubscriptionOptions highPriority;
highPriority.priority = 10;

SubscriptionOptions lowPriority;
lowPriority.priority = 0;

messenger.Subscribe<MyEvent>(handle, handler, highPriority);  // runs first
messenger.Subscribe<MyEvent>(other,  handler, lowPriority);   // runs second
```

### Exclusive Handling

Handlers that accept an `EventContext&` can prevent lower-priority handlers from running:

```cpp
SubscriptionOptions opts;
opts.priority = 100;

messenger.Subscribe<InputEvent>(
    handle, 
    [](InputEvent& event, EventContext& ctx)
    {
        if (e.key == Key::Escape)
        {
            // consume this event so no other handlers will receive it
            ctx.HandleExclusively();
        }
    }, 
    opts
);
```

Handlers that don't need propagation control can use the simpler signature - both overloads work side by side:

```cpp
messenger.Subscribe<InputEvent>(
    handle, 
    [](InputEvent& event)
    {
        // simple handler, no EventContext needed
    }
);
```

### Ephemeral/"Handle Once" Subscriptions

An ephemeral subscription fires once and then removes itself:

```cpp
SubscriptionOptions opts;
opts.ephemeral = true;

messenger.Subscribe<AppReadyEvent>(
    handle, 
    [](AppReadyEvent& event)
    {
        // runs exactly once
    }, 
    opts
);
```

### Custom Type ID Strategies

By default, Messenger identifies message types using an auto-incrementing `uint64_t` counter. This works out of the box for most projects, but some environments have their own type systems (e.g. game engines, ECS frameworks, etc.).

The `TypeIdStrategy` template parameter lets you swap in your own identification strategy without touching any library code.

#### Strategy interface

A valid strategy is any struct or class that exposes the following:

```cpp
struct MyStrategy
{
    // The type used as a key in the subscription map.
    // Must be hashable (via std::hash or a custom Hash) and equality-comparable.
    using IdType = /* your type here */;

    // A sentinel value representing "no type" or "invalid".
    // Used internally for default-constructed handles and validation checks.
    static IdType GetInvalidId();

    // Returns a unique, stable ID for each distinct type T.
    // Must return the same value for the same T across calls.
    // Must never return GetInvalidId().
    template <typename T>
    static IdType GetId();

    // Optional. If IdType is not covered by std::hash, define this
    // to provide a custom hasher for the internal unordered_map.
    // using Hash = MyCustomHasher;
};
```

#### Default strategy

`DefaultTypeIdStrategy` is the default template argument. All three declarations below are equivalent:

```cpp
Messenger<DefaultTypeIdStrategy> a;
Messenger<>                      b;
Messenger                        c;
```

#### Unreal Engine Strategy

Use `UStruct*` as the ID so message types align with Unreal's reflection system:

```cpp
struct UEStructTypeIdStrategy
{
    using IdType = UStruct*;

    static IdType GetInvalidId() { return nullptr; }

    template <typename T>
    static IdType GetId() { return T::StaticStruct(); }
};

// Convenient aliases
using UEStructMessenger = Messenger<UEStructTypeIdStrategy>;
using UEStructHandle    = SubscriptionHandle<UEStructTypeIdStrategy>;

// Usage
UEStructMessenger messenger;
UEStructHandle handle;

messenger.Subscribe<FPlayerDamagedEvent>(
    handle, 
    [](FPlayerDamagedEvent& event)
    {
        // ...
    }
);
```

Since `std::hash<UStruct*>` resolves to the default pointer hash, no custom `Hash` type is needed.

#### String-based IDs

Useful for serialization or systems where types are identified by name:

```cpp
struct StringTypeIdStrategy
{
    using IdType = std::string;

    static IdType GetInvalidId() { return {}; }

    template <typename T>
    static IdType GetId() { return typeid(T).name(); }
};

using DebugMessenger = Messenger<StringTypeIdStrategy>;
using DebugHandle    = SubscriptionHandle<StringTypeIdStrategy>;
```

Note that `typeid(T).name()` output is compiler-dependent and may be mangled. This is fine for runtime keying but shouldn't be relied on for cross-platform serialization.

#### Enum-based IDs

For projects that prefer a closed, hand-maintained set of message types:

```cpp
enum class MessageId : uint32_t
{
    Invalid    = 0,
    PlayerHit  = 1,
    EnemySpawn = 2,
    LevelLoad  = 3,
};

struct EnumTypeIdStrategy
{
    using IdType = MessageId;

    static IdType GetInvalidId() { return MessageId::Invalid; }

    template <typename T>
    static IdType GetId() { return T::kMessageId; }
};

// Message types carry their own ID as a static member
struct PlayerHitEvent
{
    static constexpr MessageId kMessageId = MessageId::PlayerHit;
    int playerId;
    float damage;
};
```

This gives you compile-time control over the mapping and makes IDs stable across runs, which is useful for network replay and serialization.

#### Custom hashers

If your `IdType` isn't covered by `std::hash`, define a `Hash` type inside your strategy:

```cpp
struct MyStrategy
{
    using IdType = MyCustomKey;

    struct Hash
    {
        size_t operator()(const MyCustomKey& key) const
        {
            return /* your hash logic */;
        }
    };

    static IdType GetInvalidId() { return MyCustomKey::None; }

    template <typename T>
    static IdType GetId() { return /* ... */; }
};
```

### Diagnostics

Query subscriber state for any message type:

```cpp
size_t count = messenger.GetSubscriberCount<MyEvent>();
bool any = messenger.HasSubscribers<MyEvent>();
```

Query and manage the message queue:

```cpp
size_t pending = messenger.GetQueuedMessageCount();
messenger.FlushQueued(); // discard all queued messages
```

# Changelog

## v2.0.0

### New Features

- **Customizable type ID strategies** - `Messenger` and `SubscriptionHandle` are now templatized on a `TypeIdPolicy`, allowing custom type identification strategies. The default policy preserves existing behavior. Custom policies can use any hashable, equality-comparable type as the ID (e.g. `UStruct*` for Unreal Engine, `std::string` via `typeid`, enum values, etc.). Policies can optionally provide a custom `Hash` type.
- **Priority-ordered handlers** - subscriptions accept a `SubscriptionOptions` struct with a `priority` field. Higher priority handlers execute first. Handlers with equal priority execute in subscription order (FIFO).
- **Exclusive handling** - handlers that accept an `EventContext&` can call `HandleExclusively()` to prevent lower-priority handlers from receiving the message.
- **Ephemeral subscriptions** - set `SubscriptionOptions::ephemeral = true` to have a subscription automatically remove itself after firing once.
- **EventContext** - handlers now receive an `EventContext` (optional) with runtime dispatch information:
    - `HandleExclusively()` / `IsHandledExclusively()` - control and query exclusive handling state.
    - `IsEphemeral()` - whether the current handler is an ephemeral subscription.
    - `GetPriority()` - the priority of the current handler.
    - `GetRemainingHandlerCount()` - how many handlers remain in the dispatch chain.
    - `IsFromQueue()` - whether the message was dispatched via `Post`/`DispatchQueued` or directly via `Dispatch`.
- **Diagnostic queries** - `GetSubscriberCount<T>()` and `HasSubscribers<T>()` allow querying subscriber state for any message type.
- **Deferred unsubscribe** - Unsubscribing will now be deferred until next dispatch.

### Bug Fixes

- **Fixed inverted guard in `Unregister()`** - the condition `if (registered_) return;` prevented unregistration of active handles.
- **Fixed moved-from handle in `Unsubscribe()`** - the handle was moved into a lambda capture before `Unregister()` was called on it, operating on a moved-from object.
- **Fixed potential deadlock in `DispatchInternal()`** - handlers are now invoked without holding the subscription lock.

### Breaking Changes

- **Handler signature** - `Subscribe` now has two overloads: `void(TMessage&, EventContext&)` (full) and `void(TMessage&)`. Existing handlers using `void(TMessage&)` will continue to work without changes.
- **`Subscribe` takes a `SubscriptionOptions` parameter** - an optional third argument. Existing code that doesn't use options is unaffected.
- **`Messenger` and `SubscriptionHandle` are now class templates** - parameterized on `TypeIdPolicy`. Both default to `DefaultTypeIdPolicy`, so `Messenger<>`, `Messenger`, and `Messenger<DefaultTypeIdPolicy>` are equivalent. Existing code using non-template `Messenger` and `SubscriptionHandle` will need to add `<>` or rely on CTAD.


## License

[MIT](LICENSE)

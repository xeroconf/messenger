# messenger
Lightweight, thread-safe, header-only C++17 messaging system.

## Dispatching
Messages are safely queued across threads via `Messenger::Post()` and all dispatched when `Messenger::DispatchQueued()` is called.
This allows flexible and deterministic message handling.

You may also dispatch the message immediately on the calling thread using `Messenger::Dispatch()`. This is suited for single-thread use cases where the queue may be redundant.

## Message Types
Define events using any non-generic type; no base class or interface required.
Each event type automatically receives a unique compile-time ID (see `aufority::msging::detail`)

## Example
```c++

struct UserJoinedEvent {
    std::string name;
}


int main() {
    // Create the messenger instance
    std::unique_ptr<aufority::msging::Messenger> msger = std::make_unique<aufority::msging::Messenger>();

    aufority::msging::SubscriptionHandle handle;

    // Subscribe to a message type.
    // Returns a subscription handle used to unsubscribe later.
    msger->Subscribe<UserJoinedEvent>(
        handle,
        [](const UserJoinedEvent& msg)
        {
            printf("Hello, %s\n", msg.name.c_str()));
        }
    );

    UserJoinedEvent msg;
    msg.name = "John";

    // Queue a message for later handling.
    msger->Post(msg);

    // Or dispatch immediately on the calling thread.
    // msger->Dispatch(msg);

    // Handle any messages that are in the queue. 
    // You'd want to execute this regularly or as often
    // as you'd like to process messages.
    msger->DispatchQueued();

    // Unsubscribe using the handle returned from Subscribe.
    msger->Unsubscribe(handle);
}
```

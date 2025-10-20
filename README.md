# messenger
A lightweight, thread-safe C++ messaging system.

## 📨 Dispatching
Messages are safely queued across threads via `Messenger::Post()` and all dispatched when `Messenger::DispatchQueued()` is called.
This allows flexible and deterministic message handling.

You may also dispatch the message immediately on the calling thread using `Messenger::Dispatch()`. This is suited for single-thread use cases where the queue may be redundant.

## 🧩 Message Types
Define events using any non-generic type; no base class or interface required.
Each event type automatically receives a unique compile-time ID (see `aufority::detail`)

## 🧪 Example
```c++

struct UserJoinedMessage {
    std::string name;
}


int main() {
    // Create the messenger instance
    aufority::Messenger msger;

    // Subscribe to a message type.
    // Returns a subscription handle used to unsubscribe later.
    auto handle = msger.Subscribe<UserJoinedMessage>(
        [](const UserJoinedMessage& msg)
        {
            printf("Hello, %s\n", msg.name.c_str());
        }
    );


    UserJoinedMessage msg;
    msg.name = "John";

    // Queue a message for later handling.
    msger.Post(msg);

    // Or dispatch immediately on the calling thread.
    msger.Dispatch(msg);

    // Handle any messages that are in the queue. 
    // You'd want to execute this regularly or as often
    // as you'd like to process messages.
    msger.DispatchQueued();

    // Unsubscribe using the handle returned from Subscribe.
    msger.Unsubscribe(handle);
}
```

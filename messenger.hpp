// =============================================================================
// Messenger - https://github.com/xeroconf/messenger
// =============================================================================
// Description:
//   A lightweight, thread-safe, header-only event messenger system.
//   Simply include this header file in your project.
//
// License:
//   MIT
//
// Author(s):
//   https://github.com/xeroconf
//
// Version:
//   2.0.0
//
// =============================================================================

#ifndef AUFORITY_MESSENGER_HPP
#define AUFORITY_MESSENGER_HPP

#include <cstdint>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <queue>
#include <functional>
#include <vector>
#include <algorithm>

namespace aufority
{
namespace msging
{
    template <typename TypeIdPolicy>
    class Messenger;

    namespace detail
    {
        /// <summary>
        /// Gets a reference to the static counter used for generating unique type IDs.
        /// </summary>
        /// <returns>
        /// A reference to the counter, initialized to 1. The counter is incremented each time
        /// a new type ID is requested.
        /// </returns>
        inline std::atomic<uint64_t>& GetUniqueIdCounter()
        {
            static std::atomic<uint64_t> kCounter{1};
            return kCounter;
        }

        /// <summary>
        /// Gets a unique, consistent ID for the specified type.
        /// </summary>
        /// <typeparam name="T">The type for which to generate or retrieve a unique ID.</typeparam>
        /// <returns>
        /// A unique ID associated with the type <typeparamref name="T"/>.
        /// The ID remains consistent for the duration of the program.
        /// </returns>
        /// <remarks>
        /// Type IDs start at 1. ID 0 is reserved and considered invalid.
        /// </remarks>
        template <typename T>
        inline uint64_t GetTypeId()
        {
            static const uint64_t kId = GetUniqueIdCounter().fetch_add(1);
            return kId;
        }

        template <typename Policy, typename = void>
        struct PolicyHash
        {
            using type = std::hash<typename Policy::IdType>;
        };

        template <typename Policy>
        struct PolicyHash<Policy, std::void_t<typename Policy::Hash>>
        {
            using type = typename Policy::Hash;
        };

        template <typename Policy>
        using PolicyHashT = typename PolicyHash<Policy>::type;
    }

    /// <summary>
    /// Default type ID policy. Assigns each message type a unique integer ID automatically.
    /// </summary>
    struct DefaultTypeIdPolicy
    {
        using IdType = uint64_t;

        /// <summary>
        /// Returns the sentinel value representing an invalid type ID.
        /// </summary>
        /// <returns>The invalid ID (0).</returns>
        static IdType GetInvalidId() { return 0; }

        /// <summary>
        /// Returns a unique, stable ID for the given type.
        /// </summary>
        /// <typeparam name="T">The message type to identify.</typeparam>
        /// <returns>A unique ID for type T.</returns>
        template <typename T>
        static IdType GetId()
        {
            return detail::GetTypeId<T>();
        }
    };

    /// <summary>
    /// Provides runtime context to a handler during dispatch.
    /// </summary>
    class EventContext
    {
    public:

        /// <summary>
        /// Constructs an EventContext with the given dispatch properties.
        /// </summary>
        /// <param name="ephemeral">Whether the current handler is an ephemeral subscription.</param>
        /// <param name="priority">The priority of the current handler.</param>
        /// <param name="remainingHandlers">The number of handlers remaining after the current one.</param>
        /// <param name="fromQueue">Whether the message was dispatched from the queue.</param>
        explicit EventContext(bool ephemeral = false, int32_t priority = 0, size_t remainingHandlers = 0, bool fromQueue = false) :
            stopped_(false),
            ephemeral_(ephemeral),
            priority_(priority),
            remainingHandlers_(remainingHandlers),
            fromQueue_(fromQueue)
        {}

        /// <summary>
        /// Prevents any remaining lower-priority handlers from receiving this message.
        /// </summary>
        void HandleExclusively() noexcept { stopped_ = true; }

        /// <summary>
        /// Returns true if this event is being handled exclusively by a handler.
        /// </summary>
        /// <returns>True if this event is being handled exclusively, false otherwise.</returns>
        bool IsHandledExclusively() const noexcept { return stopped_; }

        /// <summary>
        /// Returns true if the current handler is an ephemeral subscription
        /// that will be removed after this dispatch.
        /// </summary>
        /// <returns>True if the handler is ephemeral, false otherwise.</returns>
        bool IsEphemeral() const noexcept { return ephemeral_; }

        /// <summary>
        /// Returns the priority of the current handler.
        /// </summary>
        /// <returns>The handler's priority value.</returns>
        int32_t GetPriority() const noexcept { return priority_; }

        /// <summary>
        /// Returns the number of handlers remaining after the current one.
        /// </summary>
        /// <returns>The number of remaining handlers in the dispatch chain.</returns>
        size_t GetRemainingHandlerCount() const noexcept { return remainingHandlers_; }

        /// <summary>
        /// Returns true if the message was dispatched from the queue via Post,
        /// false if dispatched immediately via Dispatch.
        /// </summary>
        /// <returns>True if the message originated from the queue, false otherwise.</returns>
        bool IsFromQueue() const noexcept { return fromQueue_; }

    private:

        bool stopped_;
        bool ephemeral_;
        int32_t priority_;
        size_t remainingHandlers_;
        bool fromQueue_;
    };

    /// <summary>
    /// Configuration for a subscription, controlling execution order and lifetime.
    /// </summary>
    struct SubscriptionOptions
    {
        // Handler execution order. Higher values run first. Default is 0.
        int32_t priority = 0;

        // If true, the subscription is removed after firing once.
        bool ephemeral = false;
    };

    /// <summary>
    /// Represents an active subscription.
    /// </summary>
    /// <typeparam name="TypeIdPolicy">The type identification policy used by the parent Messenger.</typeparam>
    template <typename TypeIdPolicy = DefaultTypeIdPolicy>
    class SubscriptionHandle
    {
    public:

        using IdType = typename TypeIdPolicy::IdType;

        friend class Messenger<TypeIdPolicy>;

        SubscriptionHandle() :
            registered_(false),
            id_(0),
            typeId_(TypeIdPolicy::GetInvalidId()),
            messenger_(nullptr)
        {
        };

        ~SubscriptionHandle()
        {
            UnsubscribeFromMessenger();
        }

        SubscriptionHandle(SubscriptionHandle&& other) noexcept :
            registered_(other.registered_),
            id_(other.id_),
            typeId_(other.typeId_),
            messenger_(other.messenger_)
        {
            other.Reset();
        }

        SubscriptionHandle& operator=(SubscriptionHandle&& other) noexcept
        {
            if (this != &other)
            {
                UnsubscribeFromMessenger();

                registered_ = other.registered_;
                id_ = other.id_;
                typeId_ = other.typeId_;
                messenger_ = other.messenger_;

                other.Reset();
            }

            return *this;
        }

        SubscriptionHandle(const SubscriptionHandle& other) = delete;
        SubscriptionHandle& operator=(const SubscriptionHandle& other) = delete;

        /// <summary>
        /// Returns true if this handle is bound to an active subscription.
        /// </summary>
        /// <returns>True if the handle is registered, false otherwise.</returns>
        bool IsRegistered() const noexcept { return registered_; }

        /// <summary>
        /// Returns the type ID of the message this handle is subscribed to.
        /// </summary>
        /// <returns>The message type ID, or the invalid ID if not registered.</returns>
        IdType GetTypeId() const noexcept { return typeId_; }

        /// <summary>
        /// Returns the unique identifier for this subscription.
        /// </summary>
        /// <returns>The subscription handle ID, or 0 if not registered.</returns>
        uint64_t GetHandleId() const noexcept { return id_; }

    private:

        void Register(uint64_t handleId, IdType typeId, Messenger<TypeIdPolicy>* messenger) noexcept
        {
            if (registered_)
            {
                return;
            }

            if (typeId == TypeIdPolicy::GetInvalidId() || !handleId || !messenger)
            {
                return;
            }

            registered_ = true;
            id_ = handleId;
            typeId_ = typeId;
            messenger_ = messenger;
        }

        void Unregister() noexcept
        {
            if (!registered_)
            {
                return;
            }

            Reset();
        }

        void Reset() noexcept
        {
            id_ = 0;
            typeId_ = TypeIdPolicy::GetInvalidId();
            registered_ = false;
            messenger_ = nullptr;
        }

        void UnsubscribeFromMessenger()
        {
            if (registered_ && messenger_)
            {
                messenger_->Unsubscribe(*this);
            }
        }

    private:

        bool registered_;
        uint64_t id_;
        IdType typeId_;
        Messenger<TypeIdPolicy>* messenger_;

    };

    /// <summary>
    /// Central message hub. Manages subscriptions, queues messages, and dispatches
    /// them to registered handlers. Supports immediate and deferred dispatch.
    /// Templatized on a TypeIdPolicy to allow custom type identification strategies.
    /// </summary>
    /// <typeparam name="TypeIdPolicy">
    /// Policy that defines the type ID representation and lookup strategy.
    /// Must expose IdType, GetInvalidId(), and GetId&lt;T&gt;().
    /// </typeparam>
    template <typename TypeIdPolicy = DefaultTypeIdPolicy>
    class Messenger
    {
    public:

        using IdType    = typename TypeIdPolicy::IdType;
        using Handle    = SubscriptionHandle<TypeIdPolicy>;

        Messenger() :
            nextHandleId_(1),
            queueMutex_(),
            messageQueue_(),
            subscriptionMutex_(),
            subscriptions_()
        {};

        ~Messenger() = default;

        Messenger(const Messenger&) = delete;
        Messenger& operator=(const Messenger&) = delete;
        Messenger(Messenger&&) = delete;
        Messenger& operator=(Messenger&&) = delete;

        /// <summary>
        /// Subscribes a handler to a message type. The handler receives the message
        /// and an EventContext that can be used to inspect dispatch state.
        /// </summary>
        /// <typeparam name="TMessage">The message type to subscribe to.</typeparam>
        /// <param name="handle">The handle to bind the subscription to. Must not already be registered.</param>
        /// <param name="handler">The callback invoked when a matching message is dispatched.</param>
        /// <param name="options">Optional settings for priority and ephemeral behavior.</param>
        /// <returns>True if the subscription succeeded, false if the handle is already registered or the handler is empty.</returns>
        template<typename TMessage>
        bool Subscribe(
            Handle& handle,
            std::function<void(TMessage&, EventContext&)> handler,
            const SubscriptionOptions& options = {})
        {
            if (handle.IsRegistered())
            {
                return false;
            }

            if (!handler)
            {
                return false;
            }

            using DecayedT = std::decay_t<TMessage>;

            const IdType typeId = TypeIdPolicy::template GetId<DecayedT>();

            if (typeId == TypeIdPolicy::GetInvalidId())
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(subscriptionMutex_);

            uint64_t handleId = nextHandleId_.fetch_add(1);
            if (handleId == 0)
            {
                handleId = nextHandleId_.fetch_add(1);
            }

            auto wrapper = [handler](void* data, EventContext& ctx)
            {
                handler(*static_cast<TMessage*>(data), ctx);
            };

            Subscription sub(handleId, std::move(wrapper), options.priority, options.ephemeral);

            auto& subs = subscriptions_[typeId];
            auto insertPos = std::upper_bound(
                subs.begin(), subs.end(), sub,
                [](const Subscription& a, const Subscription& b)
                {
                    return a.priority > b.priority;
                }
            );
            subs.insert(insertPos, std::move(sub));

            handle.Register(handleId, typeId, this);

            return true;
        }

        /// <summary>
        /// Subscribes a handler to a message type.
        /// </summary>
        /// <typeparam name="TMessage">The message type to subscribe to.</typeparam>
        /// <param name="handle">The handle to bind the subscription to. Must not already be registered.</param>
        /// <param name="handler">The callback invoked when a matching message is dispatched.</param>
        /// <param name="options">Optional settings for priority and ephemeral behavior.</param>
        /// <returns>True if the subscription succeeded, false if the handle is already registered or the handler is empty.</returns>
        template<typename TMessage>
        bool Subscribe(
            Handle& handle,
            std::function<void(TMessage&)> handler,
            const SubscriptionOptions& options = {})
        {
            std::function<void(TMessage&, EventContext&)> wrapper =
                [handler = std::move(handler)](TMessage& msg, EventContext&)
            {
                handler(msg);
            };

            return Subscribe<TMessage>(handle, std::move(wrapper), options);
        }

        /// <summary>
        /// Removes the subscription associated with the given handle.
        /// </summary>
        /// <param name="handle">The handle whose subscription should be removed.</param>
        void Unsubscribe(Handle& handle)
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);

            if (!handle.IsRegistered())
            {
                return;
            }

            const IdType typeId = handle.GetTypeId();
            const uint64_t targetId = handle.GetHandleId();

            auto it = subscriptions_.find(typeId);
            if (it != subscriptions_.end())
            {
                auto& subs = it->second;

                subs.erase(
                    std::remove_if(
                        subs.begin(),
                        subs.end(),
                        [targetId](const Subscription& sub) { return sub.handleId == targetId; }
                    ),
                    subs.end()
                );

                if (subs.empty())
                {
                    subscriptions_.erase(it);
                }
            }

            handle.Unregister();
        }

        /// <summary>
        /// Adds a message to the queue for later dispatch. Thread-safe.
        /// </summary>
        /// <typeparam name="TMessage">The message type to post.</typeparam>
        /// <param name="message">The message to add to the queue.</param>
        template<typename TMessage>
        void Post(TMessage&& message)
        {
            using DecayedT = std::decay_t<TMessage>;
            const IdType typeId = TypeIdPolicy::template GetId<DecayedT>();
            auto messageData = std::make_shared<DecayedT>(std::forward<TMessage>(message));

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                messageQueue_.emplace(typeId, messageData);
            }
        }

        /// <summary>
        /// Dispatches a message immediately to all matching subscribers on the calling thread.
        /// </summary>
        /// <typeparam name="TMessage">The message type to dispatch.</typeparam>
        /// <param name="message">The message to dispatch.</param>
        template<typename TMessage>
        void Dispatch(TMessage&& message)
        {
            using DecayedT = std::decay_t<TMessage>;
            const IdType typeId = TypeIdPolicy::template GetId<DecayedT>();
            DecayedT local(std::forward<TMessage>(message));
            DispatchInternal(typeId, &local);
        }

        /// <summary>
        /// Dispatches all queued messages.
        /// </summary>
        void DispatchQueued()
        {
            std::queue<QueuedMessage> localQueue;

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                std::swap(localQueue, messageQueue_);
            }

            while (!localQueue.empty())
            {
                const auto& msg = localQueue.front();
                DispatchInternal(msg.typeId, msg.data.get(), true);
                localQueue.pop();
            }
        }

        /// <summary>
        /// Dispatches up to a specified number of queued messages.
        /// </summary>
        /// <param name="maxMessages">The maximum number of messages to dispatch.</param>
        void DispatchQueued(size_t maxMessages)
        {
            std::vector<QueuedMessage> messagesToProcess;
            messagesToProcess.reserve(maxMessages);

            {
                std::lock_guard<std::mutex> lock(queueMutex_);

                size_t count = 0;
                while (!messageQueue_.empty() && count < maxMessages)
                {
                    messagesToProcess.push_back(std::move(messageQueue_.front()));
                    messageQueue_.pop();
                    ++count;
                }
            }

            for (const auto& msg : messagesToProcess)
            {
                DispatchInternal(msg.typeId, msg.data.get(), true);
            }
        }

        /// <summary>
        /// Returns the number of messages waiting in the queue.
        /// </summary>
        /// <returns>The number of queued messages.</returns>
        size_t GetQueuedMessageCount() const
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            return messageQueue_.size();
        }

        /// <summary>
        /// Discards all queued messages without dispatching them.
        /// </summary>
        void FlushQueued()
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            std::queue<QueuedMessage> empty;
            std::swap(messageQueue_, empty);
        }

        /// <summary>
        /// Returns the number of active subscribers for a message type.
        /// </summary>
        /// <typeparam name="TMessage">The message type to query.</typeparam>
        /// <returns>The number of subscribers for the given message type.</returns>
        template<typename TMessage>
        size_t GetSubscriberCount() const
        {
            using DecayedT = std::decay_t<TMessage>;
            const IdType typeId = TypeIdPolicy::template GetId<DecayedT>();

            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            auto it = subscriptions_.find(typeId);
            if (it != subscriptions_.end())
            {
                return it->second.size();
            }
            return 0;
        }

        /// <summary>
        /// Returns true if there is at least one subscriber for a message type.
        /// </summary>
        /// <typeparam name="TMessage">The message type to query.</typeparam>
        /// <returns>True if at least one subscriber exists, false otherwise.</returns>
        template<typename TMessage>
        bool HasSubscribers() const
        {
            return GetSubscriberCount<TMessage>() > 0;
        }

    private:

        void DispatchInternal(IdType typeId, void* data, bool fromQueue = false)
        {
            std::vector<Subscription> handlersCopy;

            {
                std::lock_guard<std::mutex> lock(subscriptionMutex_);
                auto it = subscriptions_.find(typeId);
                if (it != subscriptions_.end())
                {
                    handlersCopy = it->second;
                }
            }

            if (handlersCopy.empty())
            {
                return;
            }

            std::vector<uint64_t> ephemeralHandles;
            const size_t totalHandlers = handlersCopy.size();

            for (size_t i = 0; i < totalHandlers; ++i)
            {
                const auto& sub = handlersCopy[i];
                const size_t remaining = totalHandlers - i - 1;

                auto ctx = EventContext(sub.ephemeral, sub.priority, remaining, fromQueue);

                sub.handler(data, ctx);

                if (sub.ephemeral)
                {
                    ephemeralHandles.push_back(sub.handleId);
                }

                if (ctx.IsHandledExclusively())
                {
                    // Handler wants to exclusively handle this event
                    break;
                }
            }

            if (!ephemeralHandles.empty())
            {
                std::lock_guard<std::mutex> lock(subscriptionMutex_);

                auto it = subscriptions_.find(typeId);
                if (it != subscriptions_.end())
                {
                    auto& subs = it->second;

                    subs.erase(
                        std::remove_if(
                            subs.begin(),
                            subs.end(),
                            [&ephemeralHandles](const Subscription& s)
                            {
                                return std::find(
                                    ephemeralHandles.begin(),
                                    ephemeralHandles.end(),
                                    s.handleId
                                ) != ephemeralHandles.end();
                            }
                        ),
                        subs.end()
                    );

                    if (subs.empty())
                    {
                        subscriptions_.erase(it);
                    }
                }
            }
        }

        struct Subscription
        {
            Subscription(
                uint64_t handleId,
                std::function<void(void*, EventContext&)> handler,
                int32_t priority,
                bool ephemeral
            ) :
                handleId(handleId),
                handler(std::move(handler)),
                priority(priority),
                ephemeral(ephemeral)
            {
            };

            ~Subscription() = default;

            uint64_t handleId;
            std::function<void(void*, EventContext&)> handler;
            int32_t priority;
            bool ephemeral;
        };

        struct QueuedMessage
        {
            template<typename T>
            QueuedMessage(IdType typeId, std::shared_ptr<T> data) :
                typeId(typeId),
                data(std::move(data))
            {
            };

            ~QueuedMessage() = default;

            IdType typeId;
            std::shared_ptr<void> data;
        };

        std::atomic<uint64_t> nextHandleId_;
        mutable std::mutex queueMutex_;
        std::queue<QueuedMessage> messageQueue_;
        mutable std::mutex subscriptionMutex_;
        std::unordered_map<IdType, std::vector<Subscription>, detail::PolicyHashT<TypeIdPolicy>> subscriptions_;
    };

} // namespace msging
} // namespace aufority

#endif

#pragma once

#include <cstdint>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <queue>
#include <functional>

namespace aufority::msging
{
    namespace detail
    {
        /// <summary>
        /// Gets a reference to the static counter used for generating unique type IDs.
        /// </summary>
        /// <returns>
        /// A reference to the counter, initialized to 1. The counter is incremented each time
        /// a new type ID is requested.
        /// </returns>
        inline uint64_t& GetUniqueIdCounter()
        {
            static uint64_t kCounter = 1;
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
            static const uint64_t kId = GetUniqueIdCounter()++;
            return kId;
        }
    }

    //---------------------------------------------------------------------------

    class SubscriptionHandle
    {
    public:

        friend class Messenger;

        SubscriptionHandle() :
            registered_(false),
            id_(0),
            typeId_(0)
        {
        };

        ~SubscriptionHandle() = default;

        SubscriptionHandle(SubscriptionHandle&& other) noexcept :
            registered_(other.registered_),
            id_(other.id_),
            typeId_(other.typeId_)
        {
            other.Reset();
        }

        SubscriptionHandle& operator=(SubscriptionHandle&& other) noexcept
        {
            if (this != &other)
            {
                registered_ = other.registered_;
                id_ = other.id_;
                typeId_ = other.typeId_;

                other.Reset();
            }

            return *this;
        }

        SubscriptionHandle(const SubscriptionHandle& other) = delete;

        SubscriptionHandle operator=(const SubscriptionHandle& other) = delete;

        bool IsRegistered() const noexcept { return registered_; }

        uint64_t GetTypeId() const noexcept { return typeId_; }

        uint64_t GetHandleId() const noexcept { return id_; }

    private:

        void Register(uint64_t handleId, uint64_t typeId) noexcept
        {
            if (registered_)
            {
                return;
            }

            if (!typeId || !handleId)
            {
                return;
            }

            registered_ = true;
            id_ = handleId;
            typeId_ = typeId;
        }

        void Unregister() noexcept
        {
            if (registered_)
            {
                return;
            }

            Reset();
        }

        void Reset() noexcept
        {
            id_ = 0;
            typeId_ = 0;
            registered_ = false;
        }

    private:

        bool registered_;
        uint64_t id_;
        uint64_t typeId_;

    };

    //---------------------------------------------------------------------------

    class Messenger
    {
    public:

        Messenger() :
            nextHandleId_(1),
            queueMutex_(),
            subscriptionMutex_(),
            messageQueue_(),
            subscriptions_()
        {};

        ~Messenger() = default;

        Messenger(const Messenger&) = delete;
        Messenger& operator=(const Messenger&) = delete;
        Messenger(Messenger&&) = delete;
        Messenger& operator=(Messenger&&) = delete;

        /// <summary>
        /// Subscribe to a specific message type.
        /// </summary>
        /// <param name="handler">The message handler.</param>
        /// <returns>Subscription handle used to unsubscribe.</returns>
        template<typename TMessage>
        bool Subscribe(SubscriptionHandle& handle, std::function<void(TMessage&)> handler)
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

            const uint64_t typeId = detail::GetTypeId<DecayedT>();

            if (typeId == 0)
            {
                return false;
            }

            std::lock_guard<std::mutex> lock(subscriptionMutex_);

            // Generate a unique handle, checking for collisions if we wrap around after exceeding uint64 limit.
            uint64_t handleId;
            do
            {
                handleId = nextHandleId_.fetch_add(1);
                // Handle wraparound - skip 0 as it is used for checking invalid handle
                // There is no collision check here as it's unlikely to wrap around with 64-bit space. We should however, consider adding this in future.
                if (handleId == 0)
                {
                    handleId = nextHandleId_.fetch_add(1);
                }
            } while (false);

            auto wrapper = [handler](void* data)
            {
                handler(*static_cast<TMessage*>(data));
            };

            subscriptions_[typeId].emplace_back(handleId, std::move(wrapper));

            handle.Register(handleId, typeId);

            return true;
        }

        /// <summary>
        /// Unsubscribe from a message type.
        /// </summary>
        /// <param name="handle">The subscription handle, acquired from subscription.</param>
        void Unsubscribe(SubscriptionHandle& handle)
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);

            if (!handle.IsRegistered())
            {
                return;
            }

            const uint64_t typeId = handle.GetTypeId();
            auto& subs = subscriptions_[typeId];
            
            subs.erase(
                std::remove_if(
                    subs.begin(),
                    subs.end(),
                    [h = std::move(handle)](const Subscription& sub) { return sub.handleId == h.GetHandleId(); }
                ),
                subs.end()
            );

            handle.Unregister();
        }

        /// <summary>
        /// Post a message to the queue. (thread-safe)
        /// </summary>
        /// <param name="message">The message to queue.</param>
        template<typename TMessage>
        void Post(TMessage&& message)
        {
            using DecayedT = std::decay_t<TMessage>;
            const uint64_t typeId = detail::GetTypeId<DecayedT>();
            auto messageData = std::make_shared<DecayedT>(std::forward<DecayedT>(message));

            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                messageQueue_.emplace(typeId, messageData);
            }
        }

       /// <summary>
       /// Dispatch a message immediately, on the calling thread.
       /// </summary>
       /// <param name="message">Message to dispatch.</param>
        template<typename TMessage>
        void Dispatch(TMessage&& message)
        {
            using DecayedT = std::decay_t<TMessage>;
            const uint64_t typeId = detail::GetTypeId<DecayedT>();
            DispatchInternal(typeId, &message);
        }

        /// <summary>
        /// Dispatch all queued messages.
        /// </summary>
        void DispatchQueued()
        {
            std::queue<QueuedMessage> localQueue;

            // Swap queues under lock to minimize lock time
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                std::swap(localQueue, messageQueue_);
            }

            // Process messages without holding the queue lock
            while (!localQueue.empty())
            {
                const auto& msg = localQueue.front();
                DispatchInternal(msg.typeId, msg.data.get());
                localQueue.pop();
            }
        }

        /// <summary>
        /// Dispatch a specific amount of messages from the queue.
        /// </summary>
        /// <param name="maxMessages">The max amount of messages to dispatch.</param>
        void DispatchQueued(size_t maxMessages)
        {
            std::vector<QueuedMessage> messagesToProcess;
            messagesToProcess.reserve(maxMessages);

            // Extract messages under lock so we can process without holding the queue lock
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
                DispatchInternal(msg.typeId, msg.data.get());
            }
        }

        /// <summary>
        /// Get the number of pending messages.
        /// </summary>
        /// <returns>Number of pending messages.</returns>
        size_t GetQueuedMessageCount() const
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            return messageQueue_.size();
        }

        /// <summary>
        /// Clear all queued messages.
        /// </summary>
        void FlushQueued()
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            std::queue<QueuedMessage> empty;
            std::swap(messageQueue_, empty);
        }

    private:

        /// <summary>
        /// Dispatches a message to all subscribers of the specified message type.
        /// </summary>
        /// <param name="typeId">The unique identifier of the message type to dispatch.</param>
        /// <param name="data">A pointer to the message data passed to each registered handler.</param>
        void DispatchInternal(uint64_t typeId, void* data)
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);

            auto it = subscriptions_.find(typeId);
            if (it != subscriptions_.end())
            {
                for (const auto& sub : it->second)
                {
                    sub.handler(data);
                }
            }
        }

        /// <summary>
        /// Represents a subscription to a specific message type.
        /// Wraps a typed handler function and allows it to be invoked.
        /// </summary>
        struct Subscription
        {
            Subscription(uint64_t handleId, std::function<void(void*)> handler) :
                handleId(handleId),
                handler(std::move(handler))
            {
            };

            ~Subscription() = default;

            uint64_t handleId;
            std::function<void(void*)> handler;
        };

        /// <summary>
        /// Represents a message that has been queued for later processing.
        /// </summary>
        struct QueuedMessage
        {
            template<typename T>
            QueuedMessage(uint64_t typeId, std::shared_ptr<T> data) :
                typeId(typeId),
                data(std::move(data))
            {
            };

            ~QueuedMessage() = default;

            uint64_t typeId;
            std::shared_ptr<void> data;
        };

    private:

        std::atomic<uint64_t> nextHandleId_;
        mutable std::mutex queueMutex_;
        std::queue<QueuedMessage> messageQueue_;
        mutable std::mutex subscriptionMutex_;
        std::unordered_map<uint64_t, std::vector<Subscription>> subscriptions_;

    };
}

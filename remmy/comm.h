#pragma once

#include <functional>
#include <memory>
#include <string>
#include "message.h"

namespace remmy {
    enum class CommErrors {
        SUCCESS = 0,
        UNKNOWN = 1,
        CONNECTION_REFUSED = 2,
        CONNECTION_ABORTED = 3,
        SEND_ERROR = 4,
        RECEIVE_ERROR = 5
    };

    const static uint32_t PKG_MAGIC_HEAD = 0x82011205;
    /**
     * A communication implementation. New communication can be implemented by deriving from
     * This class.
     *
     * \tparam EndPointT    Type of the address. You can use IP:host as in ASIO, or integer as in MPI.
     */
    template<class EndPointT>
    class CommBase {
    public:
        typedef Message<EndPointT> MessageType;
        typedef std::shared_ptr<MessageType> MessagePtr;

        CommBase(){};
        virtual ~CommBase(){};

        /** start polling for messages */
        virtual void Start()=0;

        /**   
         * Send signal to the worker threads and ask them to exit. 
         * This is used when there are multiple threads in the communication library.
        */
        virtual void SignalHandlerThreadsToExit() = 0;

        /**
         * Send a message out. The address is contained in the message.
         * 
         * \note This function MUST be thread-safe.
         *
         * \param msg   The message to be sent.
         *
         * \return  CommErrors.
         */
        virtual CommErrors Send(const MessagePtr & msg) = 0;

        /**
         * Asynchronous send. The communication layer claims ownership of the MesagePtr and returns
         * immediately. The callback function must be called after the message has been sent.
         *
         * \param msg       The message.
         * \param callback  The callback.
         */
        virtual void AsyncSend(const MessagePtr & msg, 
            const std::function<void(const MessagePtr& msg, CommErrors ec)>& callback) = 0;

        /**
         * Receives a message. The source of the message is contained in the Message struct.
         *
         * \return  A MessagePtr pointing to the Message struct.
         */
        virtual MessagePtr Recv() = 0;
    };

    /**
     * Prints the address given as ep into a string. Used for logging.
     *
     * \tparam EndPointT    Type of the end point.
     * \param ep    The address.
     *
     * \return  A const std::string.
     */
    template<class EndPointT>
    inline const std::string EPToString(const EndPointT & ep) {
        return std::to_string(ep);
    }

    template<class EndPointT>
    inline EndPointT MakeEP(const std::string& host, uint16_t port) {
        return EndPointT(host, port);
    }
};

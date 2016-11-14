#pragma once

#include <cstdint>
#include <iostream>
#include "streambuffer.h"
#include "Serialize.h"

namespace tinyrpc {
    class ProtocolBase {
    public:
        virtual uint32_t UniqueId() = 0;
        
        virtual void MarshallRequest(StreamBuffer &) = 0;

        virtual void MarshallResponse(StreamBuffer &) = 0;

        virtual void UnmarshallRequest(StreamBuffer &) = 0;

        virtual void UnmarshallResponse(StreamBuffer &) = 0;

        virtual void HandleRequest(void *server) = 0;
    };

    template<class RequestT, class ResponseT>
    class ProtocolTemplate : public ProtocolBase {
    public:
        RequestT request;
        ResponseT response;

        virtual void MarshallRequest(StreamBuffer & buf) override {
            Serialize(buf, request);
        }

        void MarshallResponse(StreamBuffer & buf) override {
            Serialize(buf, response);
        }

        virtual void UnmarshallRequest(StreamBuffer & buf) override {
            Deserialize(buf, request);
        }

        virtual void UnmarshallResponse(StreamBuffer & buf) override {
            Deserialize(buf, response);
        }
    };

    template<class RequestT>
    class ProtocolTemplate<RequestT, void> : public ProtocolBase {
    public:
        RequestT request;

        virtual void MarshallRequest(StreamBuffer & buf) override {
            Serialize(buf, request);
        }

        void MarshallResponse(StreamBuffer & buf) override {
        }

        virtual void UnmarshallRequest(StreamBuffer & buf) override {
            Deserialize(buf, request);
        }

        virtual void UnmarshallResponse(StreamBuffer & buf) override {
        }
    };

	template<uint32_t UID, class RequestT, class ResponseT = void>
	class FunctorProtocol : public ProtocolBase {
	public:
		RequestT request;
		ResponseT response;

		virtual uint32_t UniqueId() {
			return UID;
		}

		virtual void MarshallRequest(StreamBuffer& buf) {
			Serialize<RequestT>(buf, request);
		}

		virtual void MarshallResponse(StreamBuffer& buf) {
			Serialize<ResponseT>(buf, response);
		}

		virtual void UnmarshallRequest(StreamBuffer& buf) {
			Deserialize<RequestT>(buf, request);
		}

		virtual void UnmarshallResponse(StreamBuffer& buf) {
			Deserialize<ResponseT>(buf, response);
		}

		virtual void HandleRequest(void* functor) {
			std::function<ResponseT(const RequestT& req)>* f 
				= static_cast<std::function<ResponseT(const RequestT& req)>*>(functor);
			response = (*f)(request);
		}
	};

	template<uint32_t UID, class RequestT>
	class FunctorProtocol<UID, RequestT, void> : public ProtocolBase {
	public:
		RequestT request;

		virtual uint32_t UniqueId() {
			return UID;
		}

		virtual void MarshallRequest(StreamBuffer& buf) {
			Serialize<RequestT>(buf, request);
		}

		virtual void MarshallResponse(StreamBuffer& buf) {}

		virtual void UnmarshallRequest(StreamBuffer& buf) {
			Deserialize<RequestT>(buf, request);
		}

		virtual void UnmarshallResponse(StreamBuffer &) {}

		virtual void HandleRequest(void* functor) {
			std::function<void(const RequestT& req)>* f 
				= dynamic_cast<std::function<void(const RequestT& req)>*>(functor);
			f(request);
		}
	};
};
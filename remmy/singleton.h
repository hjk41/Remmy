#pragma once


namespace SimpleRPC
{

template<typename T>
class Singleton 
{
public:
    Singleton() { };
    virtual ~Singleton() { };

    static T* get_instance() {
        if (!_instance)
        {
            _instance = new T();
        }
        return _instance;
    }

    static void delete_instance() {
        if(_instance)
            delete _instance;
        _instance = NULL;
    }
protected:
    static T * _instance;
private:
    Singleton(const Singleton&);
    Singleton& operator=(const Singleton&);
};

};

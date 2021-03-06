/****************************************************************************
 Copyright (c) 2013-2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

 http://www.cocos2d-x.org

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/
#include "base/CCEventDispatcher.h"
#include <algorithm>

#include "base/CCEventCustom.h"
#include "base/CCEventListenerMouse.h"
#include "base/CCEventListenerKeyboard.h"
#include "base/CCEventListenerCustom.h"
#include "base/CCEventListenerFocus.h"
#include "base/CCEventListenerController.h"
#include "2d/CCScene.h"
#include "base/CCDirector.h"
#include "base/CCEventType.h"
#include "2d/CCCamera.h"

#define DUMP_LISTENER_ITEM_PRIORITY_INFO 0

namespace
{

class DispatchGuard
{
public:
    DispatchGuard(int& count):
            _count(count)
    {
        ++_count;
    }

    ~DispatchGuard()
    {
        --_count;
    }

private:
    int& _count;
};

}

NS_CC_BEGIN

static EventListener::ListenerID __getListenerID(Event* event)
{
    EventListener::ListenerID ret;
    switch (event->getType())
    {
        case Event::Type::CUSTOM:
        {
            auto customEvent = static_cast<EventCustom*>(event);
            ret = customEvent->getEventName();
            break;
        }
        case Event::Type::KEYBOARD:
        {
            ret = EventListenerKeyboard::LISTENER_ID;
            break;
        }
        case Event::Type::MOUSE:
        {
            ret = EventListenerMouse::LISTENER_ID;
            break;
        }
        case Event::Type::FOCUS:
        {
            ret = EventListenerFocus::LISTENER_ID;
            break;
        }
        case Event::Type::GAME_CONTROLLER:
        {
            ret = EventListenerController::LISTENER_ID;
            break;
        }
        default:
        {
            CCASSERT(false, "Invalid type!");
            break;
        }
    }
    
    return ret;
}

EventDispatcher::EventListenerVector::EventListenerVector() :
 _sceneGraphListeners(nullptr),
 _gt0Index(0)
{
}

EventDispatcher::EventListenerVector::~EventListenerVector()
{
    CC_SAFE_DELETE(_sceneGraphListeners);
}

size_t EventDispatcher::EventListenerVector::size() const
{
    size_t ret = 0;
    if (_sceneGraphListeners)
        ret += _sceneGraphListeners->size();
    
    return ret;
}

bool EventDispatcher::EventListenerVector::empty() const
{
    return (_sceneGraphListeners == nullptr || _sceneGraphListeners->empty());
}

void EventDispatcher::EventListenerVector::push_back(EventListener* listener)
{
#if CC_NODE_DEBUG_VERIFY_EVENT_LISTENERS
    CCASSERT(_sceneGraphListeners == nullptr ||
             std::count(_sceneGraphListeners->begin(), _sceneGraphListeners->end(), listener) == 0,
             "Listener should not be added twice!");
#endif

    if (_sceneGraphListeners == nullptr)
    {
        _sceneGraphListeners = new (std::nothrow) std::set<EventListener*>();
        // _sceneGraphListeners->reserve(100);
    }
    
    _sceneGraphListeners->insert(listener);
}

void EventDispatcher::EventListenerVector::clearSceneGraphListeners()
{
    if (_sceneGraphListeners)
    {
        _sceneGraphListeners->clear();
        delete _sceneGraphListeners;
        _sceneGraphListeners = nullptr;
    }
}

void EventDispatcher::EventListenerVector::clear()
{
    clearSceneGraphListeners();
}


EventDispatcher::EventDispatcher()
: _inDispatch(0)
, _isEnabled(false)
, _nodePriorityIndex(0)
{
    _toAddedListeners.reserve(50);
    _toRemovedListeners.reserve(50);
    
    // fixed #4129: Mark the following listener IDs for internal use.
    // Therefore, internal listeners would not be cleaned when removeAllEventListeners is invoked.
    _internalCustomListenerIDs.insert(EVENT_COME_TO_FOREGROUND);
    _internalCustomListenerIDs.insert(EVENT_COME_TO_BACKGROUND);
    _internalCustomListenerIDs.insert(EVENT_RENDERER_RECREATED);
}

EventDispatcher::~EventDispatcher()
{
    // Clear internal custom listener IDs from set,
    // so removeAllEventListeners would clean internal custom listeners.
    _internalCustomListenerIDs.clear();
    removeAllEventListeners();
}

void EventDispatcher::visitTarget(Node* node, bool isRootNode)
{
    node->sortAllChildren();
    
    for (auto child : node->getChildren())
    {
        visitTarget(child, false);
    }

    /*
    if (_nodeListenersMap.find(node) != _nodeListenersMap.end())
    {
        _globalZOrderNodeMap[node->getGlobalZOrder()].push_back(node);
    }
    
    if (isRootNode)
    {
        std::vector<float> globalZOrders;
        globalZOrders.reserve(_globalZOrderNodeMap.size());
        
        for (const auto& e : _globalZOrderNodeMap)
        {
            globalZOrders.push_back(e.first);
        }
        
        std::stable_sort(globalZOrders.begin(), globalZOrders.end(), [](const float a, const float b){
            return a < b;
        });
        
        for (const auto& globalZ : globalZOrders)
        {
            for (const auto& n : _globalZOrderNodeMap[globalZ])
            {
                _nodePriorityMap[n] = ++_nodePriorityIndex;
            }
        }
        
        _globalZOrderNodeMap.clear();
    }*/
}

void EventDispatcher::pauseEventListenersForTarget(Node* target, bool recursive/* = false */)
{
    auto listenerIter = _nodeListenersMap.find(target);
    if (listenerIter != _nodeListenersMap.end())
    {
        auto listeners = listenerIter->second;
        for (auto& l : *listeners)
        {
            l->setPaused(true);
        }
    }

    for (auto& listener : _toAddedListeners)
    {
        if (listener->getAssociatedNode() == target)
        {
            listener->setPaused(true);
        }
    }
    
    if (recursive)
    {
        const auto& children = target->getChildren();
        for (const auto& child : children)
        {
            pauseEventListenersForTarget(child, true);
        }
    }
}

void EventDispatcher::resumeEventListenersForTarget(Node* target, bool recursive/* = false */)
{
    auto listenerIter = _nodeListenersMap.find(target);
    if (listenerIter != _nodeListenersMap.end())
    {
        auto listeners = listenerIter->second;
        for (auto& l : *listeners)
        {
            l->setPaused(false);
        }
    }
    
    for (auto& listener : _toAddedListeners)
    {
        if (listener->getAssociatedNode() == target)
        {
            listener->setPaused(false);
        }
    }

    // setDirtyForNode(target);
    
    if (recursive)
    {
        const auto& children = target->getChildren();
        for (const auto& child : children)
        {
            resumeEventListenersForTarget(child, true);
        }
    }
}

void EventDispatcher::removeEventListenersForTarget(Node* target, bool recursive/* = false */)
{
    // Ensure the node is removed from these immediately also.
    // Don't want any dangling pointers or the possibility of dealing with deleted objects..
    _nodePriorityMap.erase(target);
    _dirtyNodes.erase(target);

    auto listenerIter = _nodeListenersMap.find(target);

    if (listenerIter != _nodeListenersMap.end())
    {
        auto listenersCopy = *listenerIter->second;
        
        for (auto& l : listenersCopy)
        {
            removeEventListener(l);
        }
    }
    
    // Bug fix: ensure there are no references to the node in the list of listeners to be added.
    // If we find any listeners associated with the destroyed node in this list then remove them.
    // This is to catch the scenario where the node gets destroyed before it's listener
    // is added into the event dispatcher fully. This could happen if a node registers a listener
    // and gets destroyed while we are dispatching an event (touch etc.)
    for (auto iter = _toAddedListeners.begin(); iter != _toAddedListeners.end(); )
    {
        EventListener * listener = *iter;
            
        if (listener->getAssociatedNode() == target)
        {
            listener->setAssociatedNode(nullptr);   // Ensure no dangling ptr to the target node.
            listener->setRegistered(false);
            releaseListener(listener);
            iter = _toAddedListeners.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
    
    if (recursive)
    {
        const auto& children = target->getChildren();
        for (const auto& child : children)
        {
            removeEventListenersForTarget(child, true);
        }
    }
}

void EventDispatcher::removeEventListenersForTargetWhere(Node* target, std::function<bool(EventListener*)> predicate, bool recursive/* = false */)
{
	auto listenerIter = _nodeListenersMap.find(target);
	if (listenerIter != _nodeListenersMap.end())
	{
		auto listeners = listenerIter->second;

		if (listeners == nullptr)
			return;

		auto listenersCopy = *listeners;
		for (auto& l : listenersCopy)
		{
			if (predicate(l))
			{
				removeEventListener(l);
			}
		}
	}
	
	for (auto iter = _toAddedListeners.begin(); iter != _toAddedListeners.end(); )
	{
		EventListener * listener = *iter;

		if (listener->getAssociatedNode() == target && predicate(*iter))
		{
			listener->setAssociatedNode(nullptr);   // Ensure no dangling ptr to the target node.
			listener->setRegistered(false);
			releaseListener(listener);
			iter = _toAddedListeners.erase(iter);
		}
		else
		{
			++iter;
		}
	}

	if (recursive)
	{
		const auto& children = target->getChildren();
		for (const auto& child : children)
		{
			removeEventListenersForTargetWhere(child, predicate, true);
		}
	}
}

void EventDispatcher::associateNodeAndEventListener(Node* node, EventListener* listener)
{
    std::vector<EventListener*>* listeners = nullptr;
    auto found = _nodeListenersMap.find(node);
    if (found != _nodeListenersMap.end())
    {
        listeners = found->second;
    }
    else
    {
        listeners = new (std::nothrow) std::vector<EventListener*>();
        _nodeListenersMap.emplace(node, listeners);
    }
    
    listeners->push_back(listener);
}

void EventDispatcher::dissociateNodeAndEventListener(Node* node, EventListener* listener)
{
    std::vector<EventListener*>* listeners = nullptr;
    auto found = _nodeListenersMap.find(node);
    if (found != _nodeListenersMap.end())
    {
        listeners = found->second;
        auto iter = std::find(listeners->begin(), listeners->end(), listener);
        if (iter != listeners->end())
        {
            listeners->erase(iter);
        }
        
        if (listeners->empty())
        {
            _nodeListenersMap.erase(found);
            delete listeners;
        }
    }
}

void EventDispatcher::addEventListener(EventListener* listener)
{
    if (_inDispatch == 0)
    {
        forceAddEventListener(listener);
    }
    else
    {
        _toAddedListeners.push_back(listener);
    }
    listener->retain();
}

void EventDispatcher::forceAddEventListener(EventListener* listener)
{
    EventListenerVector* listeners = nullptr;
    EventListener::ListenerID listenerID = listener->getListenerID();
    auto itr = _listenerMap.find(listenerID);
    if (itr == _listenerMap.end())
    {
        
        listeners = new (std::nothrow) EventListenerVector();
        _listenerMap.emplace(listenerID, listeners);
    }
    else
    {
        listeners = itr->second;
    }
    
    listeners->push_back(listener);
    
    // setDirty(listenerID, DirtyFlag::SCENE_GRAPH_PRIORITY);
    
    auto node = listener->getAssociatedNode();
    CCASSERT(node != nullptr, "Invalid scene graph priority!");
    
    associateNodeAndEventListener(node, listener);
    
    if (!node->isRunning())
    {
        listener->setPaused(true);
    }
}

void EventDispatcher::addEventListenerWithSceneGraphPriority(EventListener* listener, Node* node)
{
    CCASSERT(listener && node, "Invalid parameters.");
    CCASSERT(!listener->isRegistered(), "The listener has been registered.");
    
    if (!listener->checkAvailable())
        return;
    
    listener->setAssociatedNode(node);
    listener->setRegistered(true);
    
    addEventListener(listener);
}

#if CC_NODE_DEBUG_VERIFY_EVENT_LISTENERS && COCOS2D_DEBUG > 0

void EventDispatcher::debugCheckNodeHasNoEventListenersOnDestruction(Node* node)
{
    // Check the listeners map
    for (const auto & keyValuePair : _listenerMap)
    {
        const EventListenerVector * eventListenerVector = keyValuePair.second;
        
        if (eventListenerVector)
        {
            if (eventListenerVector->getSceneGraphPriorityListeners())
            {
                for (EventListener * listener : *eventListenerVector->getSceneGraphPriorityListeners())
                {
                    CCASSERT(!listener ||
                             listener->getAssociatedNode() != node,
                             "Node should have no event listeners registered for it upon destruction!");
                }
            }
        }
    }
    
    // Check the node listeners map
    for (const auto & keyValuePair : _nodeListenersMap)
    {
        CCASSERT(keyValuePair.first != node, "Node should have no event listeners registered for it upon destruction!");
        
        if (keyValuePair.second)
        {
            for (EventListener * listener : *keyValuePair.second)
            {
                CCASSERT(listener->getAssociatedNode() != node,
                         "Node should have no event listeners registered for it upon destruction!");
            }
        }
    }
    
    // Check the node priority map
    for (const auto & keyValuePair : _nodePriorityMap)
    {
        CCASSERT(keyValuePair.first != node,
                 "Node should have no event listeners registered for it upon destruction!");
    }
    
    // Check the to be added list
    for (EventListener * listener : _toAddedListeners)
    {
        CCASSERT(listener->getAssociatedNode() != node,
                 "Node should have no event listeners registered for it upon destruction!");
    }
    
    // Check the dirty nodes set
    for (Node * dirtyNode : _dirtyNodes)
    {
        CCASSERT(dirtyNode != node,
                 "Node should have no event listeners registered for it upon destruction!");
    }
}

#endif  // #if CC_NODE_DEBUG_VERIFY_EVENT_LISTENERS && COCOS2D_DEBUG > 0

void EventDispatcher::removeEventListener(EventListener* listener)
{
    if (listener == nullptr)
        return;
    
    // just return if listener is in _toRemovedListeners to avoid remove listeners more than once
    if (std::find(_toRemovedListeners.begin(), _toRemovedListeners.end(), listener) != _toRemovedListeners.end())
    {
        return;
    }
    
    for (auto iter = _listenerMap.begin(); iter != _listenerMap.end();)
    {
        auto listeners = iter->second;
        auto sceneGraphPriorityListeners = listeners->getSceneGraphPriorityListeners();
        bool isFound = false;

        isFound = this->removeListenerInVector(sceneGraphPriorityListeners, listener);

        /*
        if (isFound)
        {
            // fixed #4160: Dirty flag need to be updated after listeners were removed.
            setDirty(listener->getListenerID(), DirtyFlag::SCENE_GRAPH_PRIORITY);
        }*/

        if (iter->second->empty())
        {
            _priorityDirtyFlagMap.erase(listener->getListenerID());
            auto list = iter->second;
            iter = _listenerMap.erase(iter);
            CC_SAFE_DELETE(list);
        }
        else
        {
            ++iter;
        }
        
        if (isFound)
        {
            releaseListener(listener);
            return;
        }
    }

    for(auto iter = _toAddedListeners.begin(); iter != _toAddedListeners.end(); ++iter)
    {
        if (*iter == listener)
        {
            listener->setRegistered(false);
            releaseListener(listener);
            _toAddedListeners.erase(iter);
            break;
        }
    }
}

bool EventDispatcher::removeListenerInVector(std::set<EventListener*>* listeners, EventListener* listener)
{
    bool isFound = false;

    if (listeners == nullptr)
    {
        return isFound;
    }
    
    if (listeners->find(listener) != listeners->end())
    {
        CC_SAFE_RETAIN(listener);
        listener->setRegistered(false);
        if (listener->getAssociatedNode() != nullptr)
        {
            dissociateNodeAndEventListener(listener->getAssociatedNode(), listener);
            listener->setAssociatedNode(nullptr);  // nullptr out the node pointer so we don't have any dangling pointers to destroyed nodes.
        }
        
        if (_inDispatch == 0)
        {
            listeners->erase(listener);
            releaseListener(listener);
        }
        else
        {
            _toRemovedListeners.push_back(listener);
        }
        
        isFound = true;
    }

    return isFound;
}

void EventDispatcher::removeAllListenersInVector(std::set<EventListener*>* listenerVector)
{
    if (listenerVector == nullptr)
    {
        return;
    }
    
    for (auto iter = listenerVector->begin(); iter != listenerVector->end();)
    {
        auto l = *iter;
        l->setRegistered(false);

        if (l->getAssociatedNode() != nullptr)
        {
            dissociateNodeAndEventListener(l->getAssociatedNode(), l);
            l->setAssociatedNode(nullptr);  // nullptr out the node pointer so we don't have any dangling pointers to destroyed nodes.
        }
        
        if (_inDispatch == 0)
        {
            iter = listenerVector->erase(iter);
            releaseListener(l);
        }
        else
        {
            ++iter;
        }
    }
};

void EventDispatcher::dispatchEventToListeners(EventListenerVector* listeners, const std::function<bool(EventListener*)>& onEvent)
{
    bool shouldStopPropagation = false;
    auto sceneGraphPriorityListeners = listeners->getSceneGraphPriorityListeners();
    
    if (sceneGraphPriorityListeners)
    {
        if (!shouldStopPropagation)
        {
            // priority == 0, scene graph priority
            for (auto& l : *sceneGraphPriorityListeners)
            {
                if (l->isEnabled() && ((l->isGlobal() || (l->isIgnorePause() || !l->isPaused())) && l->isRegistered()) && onEvent(l))
                {
                    shouldStopPropagation = true;
                    break;
                }
            }
        }
    }
}

void EventDispatcher::dispatchMouseEventToListeners(EventListenerVector* listeners, const std::function<bool(EventListener*)>& onEvent)
{
    bool shouldStopPropagation = false;
    auto sceneGraphPriorityListeners = listeners->getSceneGraphPriorityListeners();
    
    auto scene = Director::getInstance()->getRunningScene();
    if (scene && sceneGraphPriorityListeners)
    {
        if (!shouldStopPropagation)
        {
            // priority == 0, scene graph priority
            
            // first, get all enabled, unPaused and registered listeners
            std::vector<EventListener*> sceneListeners;
            for (auto& l : *sceneGraphPriorityListeners)
            {
                if (l->isEnabled() && ((l->isGlobal() || (l->isIgnorePause() || !l->isPaused())) && l->isRegistered()))
                {
                    sceneListeners.push_back(l);
                }
            }
            // second, for all camera call all listeners
            // get a copy of cameras, prevent it's been modified in listener callback
            // if camera's depth is greater, process it earlier
            auto cameras = scene->getCameras();
            for (auto rit = cameras.rbegin(), ritRend = cameras.rend(); rit != ritRend; ++rit)
            {
                Camera* camera = *rit;
                if (camera->isVisible() == false)
                {
                    continue;
                }
                
                Camera::_visitingCamera = camera;
                auto cameraFlag = (unsigned short)camera->getCameraFlag();
                for (auto& l : sceneListeners)
                {
                    if (nullptr == l->getAssociatedNode() || 0 == (l->getAssociatedNode()->getCameraMask() & cameraFlag))
                    {
                        continue;
                    }
                    if (onEvent(l))
                    {
                        shouldStopPropagation = true;
                        break;
                    }
                }
                if (shouldStopPropagation)
                {
                    break;
                }
            }
            Camera::_visitingCamera = nullptr;
        }
    }
}

void EventDispatcher::dispatchEvent(Event* event)
{
    if (!_isEnabled)
        return;
    
    updateDirtyFlagForSceneGraph();
    
    
    DispatchGuard guard(_inDispatch);
    
    auto listenerID = __getListenerID(event);
    
    sortEventListeners(listenerID);
    
    auto pfnDispatchEventToListeners = &EventDispatcher::dispatchEventToListeners;
    if (event->getType() == Event::Type::MOUSE)
    {
        pfnDispatchEventToListeners = &EventDispatcher::dispatchMouseEventToListeners;
    }
    auto iter = _listenerMap.find(listenerID);
    if (iter != _listenerMap.end())
    {
        auto listeners = iter->second;
        
        auto onEvent = [&event](EventListener* listener) -> bool{
            event->setCurrentTarget(listener->getAssociatedNode());
            listener->_onEvent(event);
            return event->isStopped();
        };
        
        (this->*pfnDispatchEventToListeners)(listeners, onEvent);
    }
    
    updateListeners(event);
}

void EventDispatcher::dispatchCustomEvent(const std::string &eventName, void *optionalUserData)
{
    EventCustom ev(eventName);
    ev.setUserData(optionalUserData);
    dispatchEvent(&ev);
}

bool EventDispatcher::hasEventListener(const EventListener::ListenerID& listenerID) const
{
    return getListeners(listenerID) != nullptr;
}

void EventDispatcher::updateListeners(Event* event)
{
    CCASSERT(_inDispatch > 0, "If program goes here, there should be event in dispatch.");

    if (_inDispatch > 1)
        return;

    auto onUpdateListeners = [this](const EventListener::ListenerID& listenerID)
    {
        auto listenersIter = _listenerMap.find(listenerID);
        if (listenersIter == _listenerMap.end())
            return;

        auto listeners = listenersIter->second;
        auto sceneGraphPriorityListeners = listeners->getSceneGraphPriorityListeners();
        
        if (sceneGraphPriorityListeners)
        {
            for (auto iter = sceneGraphPriorityListeners->begin(); iter != sceneGraphPriorityListeners->end();)
            {
                auto l = *iter;
                if (!l->isRegistered())
                {
                    iter = sceneGraphPriorityListeners->erase(iter);
                    // if item in toRemove list, remove it from the list
                    auto matchIter = std::find(_toRemovedListeners.begin(), _toRemovedListeners.end(), l);
                    if (matchIter != _toRemovedListeners.end())
                        _toRemovedListeners.erase(matchIter);
                    releaseListener(l);
                }
                else
                {
                    ++iter;
                }
            }
        }
        
        if (sceneGraphPriorityListeners && sceneGraphPriorityListeners->empty())
        {
            listeners->clearSceneGraphListeners();
        }
    };

    onUpdateListeners(__getListenerID(event));
    
    CCASSERT(_inDispatch == 1, "_inDispatch should be 1 here.");
    
    // Zac: This code has a performance hit (iterating over a map? seriously?), and seems to never actually *do* anything. Literally always evaluates false.
    // For this reason, I'm just gonna disable this.
    /*
    for (auto iter = _listenerMap.begin(); iter != _listenerMap.end();)
    {
        if (iter->second->empty())
        {
            _priorityDirtyFlagMap.erase(iter->first);
            delete iter->second;
            iter = _listenerMap.erase(iter);
        }
        else
        {
            ++iter;
        }
    }*/
    
    if (!_toAddedListeners.empty())
    {
        for (auto& listener : _toAddedListeners)
        {
            forceAddEventListener(listener);
        }
        _toAddedListeners.clear();
    }

    if (!_toRemovedListeners.empty())
    {
        cleanToRemovedListeners();
    }
}

void EventDispatcher::updateDirtyFlagForSceneGraph()
{
    /*
    if (!_dirtyNodes.empty())
    {
        for (auto& node : _dirtyNodes)
        {
            auto iter = _nodeListenersMap.find(node);
            if (iter != _nodeListenersMap.end())
            {
                for (auto& l : *iter->second)
                {
                    setDirty(l->getListenerID(), DirtyFlag::SCENE_GRAPH_PRIORITY);
                }
            }
        }
        
        _dirtyNodes.clear();
    }
    */
}

void EventDispatcher::sortEventListeners(const EventListener::ListenerID& listenerID)
{
    /*
    return;
    DirtyFlag dirtyFlag = DirtyFlag::NONE;
    
    auto dirtyIter = _priorityDirtyFlagMap.find(listenerID);
    if (dirtyIter != _priorityDirtyFlagMap.end())
    {
        dirtyFlag = dirtyIter->second;
    }
    
    if (dirtyFlag != DirtyFlag::NONE)
    {
        // Clear the dirty flag first, if `rootNode` is nullptr, then set its dirty flag of scene graph priority
        dirtyIter->second = DirtyFlag::NONE;
        
        if ((int)dirtyFlag & (int)DirtyFlag::SCENE_GRAPH_PRIORITY)
        {
            auto rootNode = Director::getInstance()->getRunningScene();
            if (rootNode)
            {
                sortEventListenersOfSceneGraphPriority(listenerID, rootNode);
            }
            else
            {
                dirtyIter->second = DirtyFlag::SCENE_GRAPH_PRIORITY;
            }
        }
    }
    */
}

void EventDispatcher::sortEventListenersOfSceneGraphPriority(const EventListener::ListenerID& listenerID, Node* rootNode)
{
    /*
    auto listeners = getListeners(listenerID);
    
    if (listeners == nullptr)
        return;
    auto sceneGraphListeners = listeners->getSceneGraphPriorityListeners();
    
    if (sceneGraphListeners == nullptr)
        return;

    // Reset priority index
    _nodePriorityIndex = 0;
    _nodePriorityMap.clear();

    visitTarget(rootNode, true);
    
    // After sort: priority < 0, > 0
    std::stable_sort(sceneGraphListeners->begin(), sceneGraphListeners->end(), [this](const EventListener* l1, const EventListener* l2) {
        return _nodePriorityMap[l1->getAssociatedNode()] > _nodePriorityMap[l2->getAssociatedNode()];
    });
    
#if DUMP_LISTENER_ITEM_PRIORITY_INFO
    log("-----------------------------------");
    for (auto& l : *sceneGraphListeners)
    {
        log("listener priority: node ([%s]%p), priority (%d)", typeid(*l->_node).name(), l->_node, _nodePriorityMap[l->_node]);
    }
#endif
*/
}

EventDispatcher::EventListenerVector* EventDispatcher::getListeners(const EventListener::ListenerID& listenerID) const
{
    auto iter = _listenerMap.find(listenerID);
    if (iter != _listenerMap.end())
    {
        return iter->second;
    }
    
    return nullptr;
}

void EventDispatcher::removeEventListenersForListenerID(const EventListener::ListenerID& listenerID)
{
    auto listenerItemIter = _listenerMap.find(listenerID);
    if (listenerItemIter != _listenerMap.end())
    {
        auto listeners = listenerItemIter->second;
        auto sceneGraphPriorityListeners = listeners->getSceneGraphPriorityListeners();
        
        this->removeAllListenersInVector(sceneGraphPriorityListeners);
        
        // Remove the dirty flag according the 'listenerID'.
        // No need to check whether the dispatcher is dispatching event.
        _priorityDirtyFlagMap.erase(listenerID);
        
        if (!_inDispatch)
        {
            listeners->clear();
            delete listeners;
            _listenerMap.erase(listenerItemIter);
        }
    }
    
    for (auto iter = _toAddedListeners.begin(); iter != _toAddedListeners.end();)
    {
        if ((*iter)->getListenerID() == listenerID)
        {
            (*iter)->setRegistered(false);
            releaseListener(*iter);
            iter = _toAddedListeners.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

void EventDispatcher::removeEventListenersForType(EventListener::Type listenerType)
{
    if (listenerType == EventListener::Type::MOUSE)
    {
        removeEventListenersForListenerID(EventListenerMouse::LISTENER_ID);
    }
    else if (listenerType == EventListener::Type::KEYBOARD)
    {
        removeEventListenersForListenerID(EventListenerKeyboard::LISTENER_ID);
    }
    else
    {
        CCASSERT(false, "Invalid listener type!");
    }
}

void EventDispatcher::removeCustomEventListeners(const std::string& customEventName)
{
    removeEventListenersForListenerID(customEventName);
}

void EventDispatcher::removeAllEventListeners()
{
    bool cleanMap = true;
    std::vector<EventListener::ListenerID> types;
    types.reserve(_listenerMap.size());
    
    for (const auto& e : _listenerMap)
    {
        if (_internalCustomListenerIDs.find(e.first) != _internalCustomListenerIDs.end())
        {
            cleanMap = false;
        }
        else
        {
            types.push_back(e.first);
        }
    }

    for (const auto& type : types)
    {
        removeEventListenersForListenerID(type);
    }
    
    if (!_inDispatch && cleanMap)
    {
        _listenerMap.clear();
    }
}

void EventDispatcher::setEnabled(bool isEnabled)
{
    _isEnabled = isEnabled;
}

bool EventDispatcher::isEnabled() const
{
    return _isEnabled;
}

void EventDispatcher::setDirtyForNode(Node* node)
{
    /*
    // Mark the node dirty only when there is an eventlistener associated with it. 
    if (_nodeListenersMap.find(node) != _nodeListenersMap.end())
    {
        _dirtyNodes.insert(node);
    }

    // Also set the dirty flag for node's children
    const auto& children = node->getChildren();
    for (const auto& child : children)
    {
        setDirtyForNode(child);
    }
    */
}

void EventDispatcher::setDirty(const EventListener::ListenerID& listenerID, DirtyFlag flag)
{    
    auto iter = _priorityDirtyFlagMap.find(listenerID);
    if (iter == _priorityDirtyFlagMap.end())
    {
        _priorityDirtyFlagMap.emplace(listenerID, flag);
    }
    else
    {
        int ret = (int)flag | (int)iter->second;
        iter->second = (DirtyFlag) ret;
    }
}

void EventDispatcher::cleanToRemovedListeners()
{
    for (auto& l : _toRemovedListeners)
    {
        auto listenersIter = _listenerMap.find(l->getListenerID());
        if (listenersIter == _listenerMap.end())
        {
            releaseListener(l);
            continue;
        }

        bool find = false;
        auto listeners = listenersIter->second;
        auto sceneGraphPriorityListeners = listeners->getSceneGraphPriorityListeners();

        if (sceneGraphPriorityListeners)
        {
            auto machedIter = std::find(sceneGraphPriorityListeners->begin(), sceneGraphPriorityListeners->end(), l);
            if (machedIter != sceneGraphPriorityListeners->end())
            {
                find = true;
                releaseListener(l);
                sceneGraphPriorityListeners->erase(machedIter);
            }
        }

        if (find)
        {
            if (sceneGraphPriorityListeners && sceneGraphPriorityListeners->empty())
            {
                listeners->clearSceneGraphListeners();
            }
        }
        else
            CC_SAFE_RELEASE(l);
    }

    _toRemovedListeners.clear();
}

void EventDispatcher::releaseListener(EventListener* listener)
{
    CC_SAFE_RELEASE(listener);
}

NS_CC_END

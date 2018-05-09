//
// Created by Engin Manap on 13.02.2016.
//


#include "World.h"
#include "AI/HumanEnemy.h"

#include "Camera.h"
#include "GameObjects/SkyBox.h"
#include "BulletDebugDrawer.h"
#include "AI/AIMovementGrid.h"


#include "GameObjects/Players/FreeCursorPlayer.h"
#include "GameObjects/Players/FreeMovingPlayer.h"
#include "GameObjects/Players/PhysicalPlayer.h"
#include "GameObjects/Light.h"
#include "GameObjects/GameObject.h"
#include "GUI/GUILayer.h"
#include "GUI/GUIText.h"
#include "GUI/GUIFPSCounter.h"
#include "GUI/GUITextDynamic.h"
#include "ImGuiHelper.h"
#include "WorldSaver.h"
#include "../libs/ImGuizmo/ImGuizmo.h"
#include "GameObjects/TriggerObject.h"


World::World(AssetManager *assetManager, GLHelper *glHelper, Options *options)
        : assetManager(assetManager),options(options), glHelper(glHelper), fontManager(glHelper){
    // physics init
    broadphase = new btDbvtBroadphase();
    ghostPairCallback = new btGhostPairCallback();
    broadphase->getOverlappingPairCache()->setInternalGhostPairCallback(
            ghostPairCallback);    // Needed once to enable ghost objects inside Bullet

    collisionConfiguration = new btDefaultCollisionConfiguration();
    dispatcher = new btCollisionDispatcher(collisionConfiguration);

    solver = new btSequentialImpulseConstraintSolver;

    dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
    dynamicsWorld->setGravity(btVector3(0, -10, 0));
    debugDrawer = new BulletDebugDrawer(glHelper, options);
    dynamicsWorld->setDebugDrawer(debugDrawer);
    dynamicsWorld->getDebugDrawer()->setDebugMode(dynamicsWorld->getDebugDrawer()->DBG_NoDebug);
    //dynamicsWorld->getDebugDrawer()->setDebugMode(dynamicsWorld->getDebugDrawer()->DBG_MAX_DEBUG_DRAW_MODE);

    GUILayer *layer1 = new GUILayer(glHelper, debugDrawer, 1);
    layer1->setDebug(false);

    shadowMapProgramDirectional = new GLSLProgram(glHelper, "./Data/Shaders/ShadowMap/vertexDirectional.glsl",
                                                  "./Data/Shaders/ShadowMap/fragmentDirectional.glsl", false);
    shadowMapProgramPoint = new GLSLProgram(glHelper, "./Data/Shaders/ShadowMap/vertexPoint.glsl",
                                            "./Data/Shaders/ShadowMap/geometryPoint.glsl",
                                            "./Data/Shaders/ShadowMap/fragmentPoint.glsl", false);
    shadowMapProgramPoint->setUniform("farPlanePoint", options->getLightPerspectiveProjectionValues().z);

    GUIText *tr = new GUIText(glHelper, fontManager.getFont("Data/Fonts/Wolf_in_the_City_Light.ttf", 64), "Limon Engine",
                              glm::vec3(0, 0, 0));
    //tr->setScale(0.25f,0.25f);
    tr->set2dWorldTransform(glm::vec2(options->getScreenWidth()/2, options->getScreenHeight()-20), 0.0f);
    layer1->addGuiElement(tr);

    tr = new GUIText(glHelper, fontManager.getFont("Data/Fonts/Helvetica-Normal.ttf", 16), "Version 0.3",
                     glm::vec3(255, 255, 255));
    tr->set2dWorldTransform(glm::vec2(options->getScreenWidth() - 50, 100), -1 * options->PI / 2);
    layer1->addGuiElement(tr);

    cursor = new GUIText(glHelper, fontManager.getFont("Data/Fonts/Helvetica-Normal.ttf", 16), "+",
                     glm::vec3(255, 255, 255));
    cursor->set2dWorldTransform(glm::vec2(options->getScreenWidth()/2.0f, options->getScreenHeight()/2.0f), -1 * options->PI / 4);
    layer1->addGuiElement(cursor);

    trd = new GUITextDynamic(glHelper, fontManager.getFont("Data/Fonts/Helvetica-Normal.ttf", 16), glm::vec3(0, 0, 0), 640, 380, options);
    trd->set2dWorldTransform(glm::vec2(320, options->getScreenHeight()-200), 0.0f);
    layer1->addGuiElement(trd);

    physicalPlayer = new PhysicalPlayer(options, cursor);
    currentPlayer = physicalPlayer;
    camera = new Camera(options, physicalPlayer);

    //FIXME adding camera after dynamic world because static only world is needed for ai movement grid generation
    currentPlayer->registerToPhysicalWorld(dynamicsWorld, worldAABBMin, worldAABBMax);


    tr = new GUIFPSCounter(glHelper, fontManager.getFont("Data/Fonts/Helvetica-Normal.ttf", 16), "0",
                           glm::vec3(204, 204, 0));
    tr->set2dWorldTransform(glm::vec2(options->getScreenWidth() - 50, options->getScreenHeight() - 18), 0);
    layer1->addGuiElement(tr);

    guiLayers.push_back(layer1);

    /************ ImGui *****************************/
    // Setup ImGui binding
    imgGuiHelper = new ImGuiHelper(glHelper, options);
}

 bool World::checkPlayerVisibility(const glm::vec3 &from, const std::string &fromName) {
     //FIXME this debug draw creates a flicker, because we redraw frames that surpass 60. we need duration for debug draw to prevent it
     //debugDrawer->drawLine(GLMConverter::GLMToBlt(from), camera.getRigidBody()->getCenterOfMassPosition(), btVector3(1,0,0));
     btCollisionWorld::AllHitsRayResultCallback RayCallback(GLMConverter::GLMToBlt(from), GLMConverter::GLMToBlt(currentPlayer->getPosition()));

     dynamicsWorld->rayTest(
             GLMConverter::GLMToBlt(from),
             GLMConverter::GLMToBlt(currentPlayer->getPosition()),
             RayCallback
     );

     //debugDrawer->flushDraws();
     if (RayCallback.hasHit()) {
         bool hasSeen = false;
         for (int i = 0; i < RayCallback.m_collisionObjects.size(); ++i) {
             GameObject *gameObject = static_cast<GameObject *>(RayCallback.m_collisionObjects[i]->getUserPointer());
             if (gameObject->getTypeID() != GameObject::PLAYER && gameObject->getTypeID() != GameObject::TRIGGER && //trigger is ghost, so it should not block
                 gameObject->getName() != fromName) {
                 return false;
             }
             if(gameObject->getTypeID() == GameObject::PLAYER) {
                 hasSeen = true;
             }
         }
         return hasSeen;
     }
     return false;//if ray did not hit anything, return false. This should never happen
 }

 /**
  * Simulates given time (it should be constantant), reads input, animates models etc.
  * Returns true if quit requested.
  * @param simulationTimeFrame
  * @param inputHandler
  * @return
  */
bool World::play(Uint32 simulationTimeFrame, InputHandler &inputHandler) {
    if(currentMode != EDITOR_MODE && currentMode != PAUSED_MODE) {
        //every time we call this method, we increase the time only by simulationTimeframe
        gameTime += simulationTimeFrame;
        dynamicsWorld->stepSimulation(simulationTimeFrame / 1000.0f);
        currentPlayer->processPhysicsWorld(dynamicsWorld);

        for (auto actorIt = actors.begin(); actorIt != actors.end(); ++actorIt) {
            ActorInformation information = fillActorInformation(actorIt->second);
            actorIt->second->play(gameTime, information, options);
        }
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            if (!it->second->getRigidBody()->isStaticOrKinematicObject()) {
                it->second->updateTransformFromPhysics();
            }
            it->second->setIsInFrustum(glHelper->isInFrustum(it->second->getAabbMin(), it->second->getAabbMax()));
            if(it->second->isIsInFrustum()) {
                it->second->setupForTime(gameTime);
            }

        }
    } else {
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            it->second->setIsInFrustum(glHelper->isInFrustum(it->second->getAabbMin(), it->second->getAabbMax()));
        }
            dynamicsWorld->updateAabbs();
    }

    for (unsigned int i = 0; i < guiLayers.size(); ++i) {
        guiLayers[i]->setupForTime(gameTime);
    }

    for(auto trigger = triggers.begin(); trigger != triggers.end(); trigger++) {
        trigger->second->checkAndTrigger();
    }

    //end of physics step

    // If not in editor mode, dont let imgGuiHelper get input
    // if in editor mode, but player press editor button, dont allow imgui to process input
    // if in editor mode, player did not press editor button, then check if imgui processed, if not use the input
    if(currentMode != EDITOR_MODE || inputHandler.getInputEvents(InputHandler::EDITOR) || !imgGuiHelper->ProcessEvent(inputHandler)) {
            if(handlePlayerInput(inputHandler)) {
                isQuitRequest = !isQuitRequest;
            }
    }
    return isQuitRequest && isQuitVerified;
}

ActorInformation World::fillActorInformation(Actor *actor) {
    ActorInformation information;
    information.canSeePlayerDirectly = checkPlayerVisibility(actor->getPosition()+ AIMovementGrid::floatingHeight, actor->getModel()->getName());
    glm::vec3 front = actor->getFrontVector();
    glm::vec3 rayDir = currentPlayer->getPosition() - actor->getPosition();
    float cosBetween = glm::dot(normalize(front), normalize(rayDir));
    information.cosineBetweenPlayer = cosBetween;
    information.playerDirection = normalize(rayDir);
    if(cosBetween > 0) {
            information.isPlayerFront = true;
            information.isPlayerBack = false;
        } else {
            information.isPlayerFront = false;
            information.isPlayerBack = true;
        }
    //now we know if it is front or back. we can check up, down, left, right
    //remove the y component, and test for left, right
    glm::vec3 rayDirWithoutY = rayDir;
    rayDirWithoutY.y = 0;
    glm::vec3 frontWithoutY = front;
    frontWithoutY.y = 0;
    glm::vec3 crossBetween = cross(normalize(frontWithoutY), normalize(rayDirWithoutY));
    float cosineForSide = glm::dot(normalize(frontWithoutY), normalize(rayDirWithoutY));
    information.cosineBetweenPlayerForSide = cosineForSide;
    if(crossBetween.y > 0){
            information.isPlayerRight = false;
            information.isPlayerLeft = true;
        } else {
            information.isPlayerRight = true;
            information.isPlayerLeft = false;
        }
    //now we need up and down. For that, normally we can remove z or x, but since camera is z alone at start, I will use x
    rayDir.x = 0;
    front.x = 0;
    crossBetween = cross(normalize(front), normalize(rayDir));
    if(crossBetween.x > 0){
            information.isPlayerUp = false;
            information.isPlayerDown = true;
        } else {
            information.isPlayerUp = true;
            information.isPlayerDown = false;
        }
    std::vector<glm::vec3> route;
    glm::vec3 playerPosWithGrid = currentPlayer->getPosition();
    bool isPlayerReachable = grid->setProperHeight(&playerPosWithGrid, AIMovementGrid::floatingHeight, 0.0f, dynamicsWorld);
    if(isPlayerReachable && grid->coursePath(actor->getPosition() + glm::vec3(0, AIMovementGrid::floatingHeight, 0), playerPosWithGrid, actor->getWorldID(), &route)) {
        if (route.empty()) {
            information.toPlayerRoute = glm::vec3(0, 0, 0);
            information.canGoToPlayer = false;
        } else {
            //Normally, this information should be used for straightening the path, but not yet.
            information.toPlayerRoute = route[route.size() - 1] - actor->getPosition() - glm::vec3(0, 2.0f, 0);
            information.canGoToPlayer = true;
        }
    }
    return information;
}

bool World::handlePlayerInput(InputHandler &inputHandler) {
    if(!isQuitRequest && isQuitVerified) {
        isQuitVerified = false;
        //means player selected stay, we should revert to last player type
        switch (beforeMode) {
            case DEBUG_MODE:
                switchToDebugMode(inputHandler);
                break;
            case EDITOR_MODE:
                switchToEditorMode(inputHandler);
                break;
            case PHYSICAL_MODE:
            default:
                switchToPhysicalPlayer(inputHandler);
                break;
        }
    }
    if(inputHandler.getInputEvents(inputHandler.MOUSE_BUTTON_LEFT)) {
        if(inputHandler.getInputStatus(inputHandler.MOUSE_BUTTON_LEFT)) {
            GameObject *gameObject = getPointedObject();
            if (gameObject != nullptr) {
                pickedObject = gameObject;
            } else {
                pickedObject = nullptr;
            }
        }
    }


    if (inputHandler.getInputEvents(inputHandler.EDITOR) && inputHandler.getInputStatus(inputHandler.EDITOR)) {
        if(currentMode != EDITOR_MODE ) {
            switchToEditorMode(inputHandler);
        } else {
            switch (beforeMode) {
                case DEBUG_MODE: {
                    switchToDebugMode(inputHandler);
                    break;
                }
                case PHYSICAL_MODE:
                default: {
                    switchToPhysicalPlayer(inputHandler);//if double editor, return to physical. This can happen when try to quit
                }
            }
        }
    }
    //if not in editor mode and press debug
    if (currentMode != EDITOR_MODE && inputHandler.getInputEvents(inputHandler.DEBUG) && inputHandler.getInputStatus(inputHandler.DEBUG)) {
        if(currentMode != DEBUG_MODE) {
            switchToDebugMode(inputHandler);
        } else {
            //no matter what was the before, just return to player
            switchToPhysicalPlayer(inputHandler);
        }
    }

    float xPosition, yPosition, xChange, yChange;
    if (inputHandler.getMouseChange(xPosition, yPosition, xChange, yChange)) {
        currentPlayer->rotate(xPosition, yPosition, xChange, yChange);
    }

    if (inputHandler.getInputEvents(inputHandler.RUN)) {
        if(inputHandler.getInputStatus(inputHandler.RUN)) {
            options->setMoveSpeed(Options::RUN);
        } else {
            options->setMoveSpeed(Options::WALK);
        }
    }

    PhysicalPlayer::moveDirections direction = PhysicalPlayer::NONE;
    //ignore if both are pressed.
    if (inputHandler.getInputStatus(inputHandler.MOVE_FORWARD) !=
        inputHandler.getInputStatus(inputHandler.MOVE_BACKWARD)) {
        if (inputHandler.getInputStatus(inputHandler.MOVE_FORWARD)) {
            direction = Player::FORWARD;
        } else {
            direction = Player::BACKWARD;
        }
    }
    if (inputHandler.getInputStatus(inputHandler.MOVE_LEFT) != inputHandler.getInputStatus(inputHandler.MOVE_RIGHT)) {
        if (inputHandler.getInputStatus(inputHandler.MOVE_LEFT)) {
            if (direction == Player::FORWARD) {
                direction = Player::LEFT_FORWARD;
            } else if (direction == Player::BACKWARD) {
                direction = Player::LEFT_BACKWARD;
            } else {
                direction = Player::LEFT;
            }
        } else if (direction == Player::FORWARD) {
            direction = Player::RIGHT_FORWARD;
        } else if (direction == Player::BACKWARD) {
            direction = Player::RIGHT_BACKWARD;
        } else {
            direction = Player::RIGHT;
        }
    }

    if (inputHandler.getInputStatus(inputHandler.JUMP) && inputHandler.getInputEvents(inputHandler.JUMP)) {
        direction = Player::UP;
    }

    //if none, camera should handle how to get slower.
    currentPlayer->move(direction);

    if(inputHandler.getInputEvents(inputHandler.QUIT) &&  inputHandler.getInputStatus(inputHandler.QUIT)) {
        if(currentMode != EDITOR_MODE) {
            switchToEditorMode(inputHandler);

        } else {
            beforeMode = EDITOR_MODE;//you should return to editor mode after quitting
        }
        return true;
    } else {
        return false;
    }
}

void World::switchToDebugMode(InputHandler &inputHandler) {
    dynamicsWorld->getDebugDrawer()->setDebugMode(
            dynamicsWorld->getDebugDrawer()->DBG_MAX_DEBUG_DRAW_MODE | dynamicsWorld->getDebugDrawer()->DBG_DrawAabb | dynamicsWorld->getDebugDrawer()->DBG_DrawConstraints | dynamicsWorld->getDebugDrawer()->DBG_DrawConstraintLimits);
    options->getLogger()->log(Logger::log_Subsystem_INPUT, Logger::log_level_INFO, "Debug enabled");
    guiLayers[0]->setDebug(true);
    //switch control to debug player
    if(debugPlayer == nullptr) {
                debugPlayer = new FreeMovingPlayer(options, cursor);
                debugPlayer->registerToPhysicalWorld(dynamicsWorld, worldAABBMin, worldAABBMax);
            }
    debugPlayer->ownControl(currentPlayer->getPosition(), currentPlayer->getLookDirection());
    currentPlayer = debugPlayer;
    camera->setCameraAttachment(debugPlayer);
    inputHandler.setMouseModeRelative();
    beforeMode = currentMode;
    currentMode = DEBUG_MODE;
}

void World::switchToPhysicalPlayer(InputHandler &inputHandler) {
    physicalPlayer->ownControl(currentPlayer->getPosition(), currentPlayer->getLookDirection());
    currentPlayer = physicalPlayer;
    camera->setCameraAttachment(physicalPlayer);
    dynamicsWorld->updateAabbs();
    inputHandler.setMouseModeRelative();
    this->dynamicsWorld->getDebugDrawer()->setDebugMode(this->dynamicsWorld->getDebugDrawer()->DBG_NoDebug);
    this->guiLayers[0]->setDebug(false);
    beforeMode = currentMode;
    currentMode = PHYSICAL_MODE;
}

void World::switchToEditorMode(InputHandler &inputHandler) {//switch control to free cursor player
    if(editorPlayer == nullptr) {
                editorPlayer = new FreeCursorPlayer(options, cursor);
                editorPlayer->registerToPhysicalWorld(dynamicsWorld, worldAABBMin, worldAABBMax);
            }
    editorPlayer->ownControl(currentPlayer->getPosition(), currentPlayer->getLookDirection());
    currentPlayer = editorPlayer;
    camera->setCameraAttachment(editorPlayer);
    inputHandler.setMouseModeFree();
    beforeMode = currentMode;
    currentMode = EDITOR_MODE;
}

GameObject * World::getPointedObject() const {
    glm::vec3 from, lookDirection;
    currentPlayer->getWhereCameraLooks(from, lookDirection);
    //we want to extend to vector to world AABB limit
    float maxFactor = 0;

    if(lookDirection.x > 0 ) {
        //so we are looking at positive x. determine how many times the ray x we need
        maxFactor = (worldAABBMax.x - from.x) / lookDirection.x;
    } else {
        maxFactor = (worldAABBMin.x - from.x) / lookDirection.x; //Mathematically this should be (from - world.min) / -1 * lookdir, but it cancels out
    }

    if(lookDirection.y > 0 ) {
        std::max(maxFactor, (worldAABBMax.y - from.y) / lookDirection.y);
    } else {
        std::max(maxFactor, (worldAABBMin.y - from.y) / lookDirection.y);//Mathematically this should be (from - world.min) / -1 * lookdir, but it cancels out
    }

    if(lookDirection.z > 0 ) {
        std::max(maxFactor, (worldAABBMax.z - from.z) / lookDirection.z);
    } else {
        std::max(maxFactor, (worldAABBMin.z - from.z) / lookDirection.z);//Mathematically this should be (from - world.min) / -1 * lookdir, but it cancels out
    }
    lookDirection = lookDirection * maxFactor;
    glm::vec3 to = lookDirection + from;
    btCollisionWorld::ClosestRayResultCallback RayCallback(GLMConverter::GLMToBlt(from), GLMConverter::GLMToBlt(to));

    dynamicsWorld->rayTest(
                GLMConverter::GLMToBlt(from),
                GLMConverter::GLMToBlt(to),
                RayCallback
        );

    //debugDrawer->flushDraws();
    if (RayCallback.hasHit()) {
        return static_cast<GameObject *>(RayCallback.m_collisionObject->getUserPointer());
    } else {
        return nullptr;
    }
}

void World::render() {
    for (unsigned int i = 0; i < lights.size(); ++i) {
        if(lights[i]->getLightType() != Light::DIRECTIONAL) {
            continue;
        }
        //generate shadow map
        glHelper->switchRenderToShadowMapDirectional(i);
        shadowMapProgramDirectional->setUniform("renderLightIndex", (int)i);
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            (*it).second->renderWithProgram(*shadowMapProgramDirectional);
        }
    }

    for (unsigned int i = 0; i < lights.size(); ++i) {
        if(lights[i]->getLightType() != Light::POINT) {
            continue;
        }
        //generate shadow map
        glHelper->switchRenderToShadowMapPoint();
        //FIXME why are these set here?
        shadowMapProgramPoint->setUniform("renderLightIndex", (int)i);
        //FIXME this is suppose to be an option //FarPlanePoint is set at declaration, since it is a constant
        shadowMapProgramPoint->setUniform("farPlanePoint", 100.0f);
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            (*it).second->renderWithProgram(*shadowMapProgramPoint);
        }
    }

    if(camera->isDirty()) {
        glHelper->setPlayerMatrices(camera->getPosition(), camera->getCameraMatrix());//this is required for any render
    }

    glHelper->switchRenderToDefault();
    if(sky!=nullptr) {
        sky->render();//this is moved to the top, because transparency can create issues if this is at the end
    }

    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if(it->second->isIsInFrustum()) {
            (*it).second->render();
        }
    }

    dynamicsWorld->debugDrawWorld();
    if (this->dynamicsWorld->getDebugDrawer()->getDebugMode() != btIDebugDraw::DBG_NoDebug) {
        debugDrawer->drawLine(btVector3(0, 0, 0), btVector3(0, 250, 0), btVector3(1, 1, 1));
        //draw the ai-grid
        grid->debugDraw(debugDrawer);
    }

    debugDrawer->flushDraws();


    //since gui uses blending, everything must be already rendered.
    // Also, since gui elements only depth test each other, clear depth buffer
    glHelper->clearDepthBuffer();

    for (std::vector<GUILayer *>::iterator it = guiLayers.begin(); it != guiLayers.end(); ++it) {
        (*it)->render();
    }
    if(currentMode == EDITOR_MODE) {
        ImGuiFrameSetup();
    }
}

/**
 * This method checks if we are in editor mode, and if we are, enables ImGui windows
 * It also fills the windows with relevant parameters.
 */
void World::ImGuiFrameSetup() {//TODO not const because it removes the object. Should be seperated
    if(this->isQuitRequest) {
        if(isQuitRequest) {
            //ask if wants to save
            imgGuiHelper->NewFrame();
            ImGui::Begin("Quitting, Are you sure?");
            if(ImGui::Button("Yes, quit")) {
                isQuitVerified = true;
            }
            ImGui::SameLine();
            if(ImGui::Button("No, stay")) {
                isQuitRequest = false;
                isQuitVerified = true;
                currentMode = PAUSED_MODE;//FIXME we are rendering more than we allow input. That causes a bit delay before we exit editor mode, which in turn creates a bit shutter.
                //this line should have 0 effect because it will be overriden, but it will remove the shutter.
                //return to the player we left off in next frame
            }
            ImGui::End();
            imgGuiHelper->RenderDrawLists();
        }
    } else {
        if(!availableAssetsLoaded) {
            assetManager->loadAssetList("./Data/AssetList.xml");
            availableAssetsLoaded = true;
        }
        imgGuiHelper->NewFrame();

        /* window definitions */
        {
            ImGui::Begin("Editor");
            //list available elements
            static std::string selectedAssetFile = "";
            glm::vec3 newObjectPosition = camera->getPosition() + 10.0f * camera->getCenter();

            if (ImGui::CollapsingHeader("Add New Object")) {
                if (ImGui::BeginCombo("Available objects", selectedAssetFile.c_str())) {
                    for (auto it = assetManager->getAvailableAssetsList().begin();
                         it != assetManager->getAvailableAssetsList().end(); it++) {
                        if (ImGui::Selectable(it->first.c_str())) {
                            selectedAssetFile = it->first;
                        }
                    }
                    ImGui::EndCombo();
                }
                static float newObjectWeight;
                ImGui::SliderFloat("Weight", &newObjectWeight, 0.0f, 100.0f);

                ImGui::NewLine();
                if(selectedAssetFile != "") {
                    if(ImGui::Button("Add Object")) {
                        Model* newModel = new Model(this->getNextObjectID(), assetManager, newObjectWeight, selectedAssetFile);
                        newModel->setTranslate(newObjectPosition);
                        this->addModelToWorld(newModel);
                        newModel->getRigidBody()->activate();
                        pickedObject = static_cast<GameObject*>(newModel);
                    }
                }
            }

            if(ImGui::Button("Add Trigger")) {

                TriggerObject* to = new TriggerObject(this->getNextObjectID());
                to->setTranslate(newObjectPosition);
                this->dynamicsWorld->addCollisionObject(to->getGhostObject(), btBroadphaseProxy::SensorTrigger,
                                                        btBroadphaseProxy::AllFilter & ~btBroadphaseProxy::SensorTrigger);
                triggers[to->getWorldObjectID()] = to;

                pickedObject = static_cast<GameObject*>(to);
            }

            ImGui::SetNextWindowSize(ImVec2(0,0), true);//true means set it only once

            ImGui::Begin("Selected Object Properties");
            bool isObjectSelectorOpen;
            if(pickedObject == nullptr) {
                isObjectSelectorOpen = ImGui::BeginCombo("Picked object", "No object selected");
            } else {
                isObjectSelectorOpen =ImGui::BeginCombo("Picked object", (pickedObject->getName().c_str()));
            }
            if (isObjectSelectorOpen) {
                for (auto it = objects.begin(); it != objects.end(); it++) {
                    GameObject* gameObject = dynamic_cast<GameObject *>(it->second);
                    if (ImGui::Selectable(gameObject->getName().c_str())) {
                        pickedObject = gameObject;
                    }
                }
                for (auto it = lights.begin(); it != lights.end(); it++) {
                    GameObject* gameObject = dynamic_cast<GameObject *>(*it);
                    if (ImGui::Selectable(gameObject->getName().c_str())) {
                        pickedObject = (*it);
                    }
                }
                ImGui::EndCombo();
            }
            if(pickedObject != nullptr) {
                GameObject::ImGuiResult request = pickedObject->addImGuiEditorElements();
                if (request.isGizmoRequired) {
                    ImGuizmoFrameSetup(request);
                }
                if (request.removeAI) {
                    //remove AI requested
                    if (dynamic_cast<Model *>(pickedObject)->getAIID() != 0) {
                        actors.erase(dynamic_cast<Model *>(pickedObject)->getAIID());
                        dynamic_cast<Model *>(pickedObject)->detachAI();
                    }
                }

                if (request.addAI) {
                    std::cout << "adding AI to model " << std::endl;
                    HumanEnemy *newEnemy = new HumanEnemy(getNextObjectID());
                    newEnemy->setModel(dynamic_cast<Model *>(pickedObject));

                    addActor(newEnemy);
                    ImGui::NewLine();
                    if (pickedObject->getTypeID() == GameObject::MODEL) {
                        if (ImGui::Button("Remove This Object")) {
                            //remove the object.
                            PhysicalRenderable *removeObject = objects[pickedObject->getWorldObjectID()];
                            //disconnect from physics
                            dynamicsWorld->removeRigidBody(removeObject->getRigidBody());
                            //disconnect AI
                            if (dynamic_cast<Model *>(removeObject)->getAIID() != 0) {
                                actors.erase(dynamic_cast<Model *>(removeObject)->getAIID());
                            }
                            objects.erase(pickedObject->getWorldObjectID());
                            pickedObject = nullptr;
                            delete removeObject;
                        }
                    }
                }
            }


            ImGui::End();
            ImGui::NewLine();
            if(ImGui::Button("Save Map")) {
                if(WorldSaver::saveWorld("./Data/Maps/CustomWorld001.xml", this)) {
                    options->getLogger()->log(Logger::log_Subsystem_LOAD_SAVE, Logger::log_level_INFO, "World save successful");
                } else {
                    options->getLogger()->log(Logger::log_Subsystem_LOAD_SAVE, Logger::log_level_ERROR, "World save Failed");
                }
            }
            ImGui::End();
        }

        /* window definitions */
        imgGuiHelper->RenderDrawLists();
    }
}

void World::ImGuizmoFrameSetup(const GameObject::ImGuiResult& request) {

    ImGuizmo::OPERATION mCurrentGizmoOperation = ImGuizmo::TRANSLATE;

    switch (request.mode) {
        case GameObject::TRANSLATE_MODE:
            mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
            break;
        case GameObject::ROTATE_MODE:
            mCurrentGizmoOperation = ImGuizmo::ROTATE;
            break;
        case GameObject::SCALE_MODE:
            mCurrentGizmoOperation = ImGuizmo::SCALE;
            break;
    }
    glm::mat4 cameraMatrix = camera->getCameraMatrix();
    glm::mat4 perspectiveMatrix = glHelper->getProjectionMatrix();
    glm::mat4 objectMatrix;
    ImGuizmo::BeginFrame();
    glm::vec3 eulerRotation;
    switch (pickedObject->getTypeID()) {
        case GameObject::ObjectTypes::MODEL: {
            eulerRotation = glm::eulerAngles(dynamic_cast<Model *>(pickedObject)->getOrientation());
            eulerRotation = eulerRotation * 57.2957795f;
            ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(dynamic_cast<Model *>(pickedObject)->getTranslate()),
                                                    glm::value_ptr(eulerRotation),
                                                    glm::value_ptr(dynamic_cast<Model *>(pickedObject)->getScale()),
                                                    glm::value_ptr(objectMatrix));
            break;
        }
        case GameObject::ObjectTypes::LIGHT: {
            ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(dynamic_cast<Light *>(pickedObject)->getPosition()),
                                                    glm::value_ptr(glm::vec3(0.0f)),
                                                    glm::value_ptr(glm::vec3(1.0f)),
                                                    glm::value_ptr(objectMatrix));
            break;
        }
        case GameObject::ObjectTypes::TRIGGER: {
            eulerRotation = glm::eulerAngles(dynamic_cast<TriggerObject *>(pickedObject)->getOrientation());
            eulerRotation = eulerRotation * 57.2957795f;
            ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(dynamic_cast<TriggerObject *>(pickedObject)->getTranslate()),
                                                    glm::value_ptr(eulerRotation),
                                                    glm::value_ptr(dynamic_cast<TriggerObject *>(pickedObject)->getScale()),
                                                    glm::value_ptr(objectMatrix));
            break;
        }
        default:
            return;//we can't work without a way to define transform matrix
    }


    static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);

    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    float tempSnap[3] = {request.snap[0], request.snap[1], request.snap[2] };
    ImGuizmo::Manipulate(glm::value_ptr(cameraMatrix), glm::value_ptr(perspectiveMatrix), mCurrentGizmoOperation, mCurrentGizmoMode, glm::value_ptr(objectMatrix), NULL, request.useSnap ? &(tempSnap[0]) : NULL);

    //now we should have object matrix updated, update the object

    glm::vec3 scale, translate;
    glm::quat rotation;
    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(objectMatrix), glm::value_ptr(translate), glm::value_ptr(eulerRotation), glm::value_ptr(scale));
    rotation = glm::quat(eulerRotation / 57.2957795f);
    switch (mCurrentGizmoOperation) {
        case ImGuizmo::TRANSLATE:
            switch (pickedObject->getTypeID()) {
                case GameObject::ObjectTypes::MODEL: {
                    dynamic_cast<Model *>(pickedObject)->setTranslate(translate);
                    break;
                }
                case GameObject::ObjectTypes::LIGHT: {
                    dynamic_cast<Light *>(pickedObject)->setPosition(translate);
                    break;
                }

                case GameObject::ObjectTypes::TRIGGER: {
                    dynamic_cast<TriggerObject *>(pickedObject)->setTranslate(translate);
                    break;
                }

                default: {
                    std::cerr << "Translated unexpected object. Report to developer." << std::endl;
                    exit(-1);
                }
            }
            break;
        case ImGuizmo::ROTATE:
            switch (pickedObject->getTypeID()) {
                case GameObject::ObjectTypes::MODEL: {
                    dynamic_cast<Model *>(pickedObject)->setOrientation(rotation);
                    break;
                }
                case GameObject::ObjectTypes::TRIGGER: {
                    dynamic_cast<TriggerObject *>(pickedObject)->setOrientation(rotation);
                    break;
                }
                default: {
                    std::cerr << "ROTATED unexpected object. Report to developer." << std::endl;
                    exit(-1);
                }
            }

            break;
        case ImGuizmo::SCALE:
            switch (pickedObject->getTypeID()) {
                case GameObject::ObjectTypes::MODEL: {
                    dynamic_cast<Model *>(pickedObject)->setScale(scale);
                    break;
                }
                case GameObject::ObjectTypes::TRIGGER: {
                    dynamic_cast<TriggerObject *>(pickedObject)->setScale(scale);
                    break;
                }
                default: {
                    std::cerr << "SCALED unexpected object. Report to developer." << std::endl;
                    exit(-1);
                }
            }
            break;
    }
}

World::~World() {
    delete dynamicsWorld;

    //FIXME clear GUIlayer elements
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        delete (*it).second;
    }

    for (auto it = triggers.begin(); it != triggers.end(); ++it) {
        delete (*it).second;
    }
    
    delete sky;

    for (std::vector<Light *>::iterator it = lights.begin(); it != lights.end(); ++it) {
        delete (*it);
    }
    delete debugDrawer;
    delete solver;
    delete collisionConfiguration;
    delete dispatcher;
    delete broadphase;
    delete ghostPairCallback;

    delete grid;
    delete camera;
    delete physicalPlayer;
    if(debugPlayer!= nullptr) {
        delete debugPlayer;
    }
    delete imgGuiHelper;
}

void World::addModelToWorld(Model *xmlModel) {
    xmlModel->getWorldTransform();
    objects[xmlModel->getWorldObjectID()] = xmlModel;
    rigidBodies.push_back(xmlModel->getRigidBody());
    dynamicsWorld->addRigidBody(xmlModel->getRigidBody());
    btVector3 aabbMin, aabbMax;
    xmlModel->getRigidBody()->getAabb(aabbMin, aabbMax);
    std::cout << "bounding box of model " << xmlModel->getName() << " is "
              << GLMUtils::vectorToString(GLMConverter::BltToGLM(aabbMin)) << ", "
              << GLMUtils::vectorToString(GLMConverter::BltToGLM(aabbMax)) << std::endl;
    updateWorldAABB(GLMConverter::BltToGLM(aabbMin), GLMConverter::BltToGLM(aabbMax));
}

void World::updateWorldAABB(glm::vec3 aabbMin, glm::vec3 aabbMax) {
    worldAABBMin = glm::vec3(std::min(aabbMin.x, worldAABBMin.x), std::min(aabbMin.y, worldAABBMin.y), std::min(aabbMin.z, worldAABBMin.z));
    worldAABBMax = glm::vec3(std::max(aabbMax.x, worldAABBMax.x), std::max(aabbMax.y, worldAABBMax.y), std::max(aabbMax.z, worldAABBMax.z));
}

void World::addActor(Actor *actor) {
    this->actors[actor->getWorldID()] = actor;
}

void World::createGridFrom(const glm::vec3 &aiGridStartPoint) {
    if(grid != nullptr) {
        delete grid;
    }
    grid = new AIMovementGrid(aiGridStartPoint, dynamicsWorld, worldAABBMin, worldAABBMax);
}

void World::setSky(SkyBox *skyBox) {
    if(sky!= nullptr) {
        delete sky;
    }
    sky = skyBox;
}

void World::addLight(Light *light) {
    glHelper->setLight(*(light), lights.size());//since size start from 0, this should be before adding it to vector
    this->lights.push_back(light);
}

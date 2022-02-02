/*
**
** Copyright 2017, Samsung Electronics Co. LTD
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

/* #define LOG_NDEBUG 0 */
#define LOG_TAG "ExynosCameraFrameFactoryPreview"
#include <cutils/log.h>

#include "ExynosCameraFrameFactoryPreview.h"

namespace android {

ExynosCameraFrameFactoryPreview::~ExynosCameraFrameFactoryPreview()
{
    status_t ret = NO_ERROR;

    if (m_shot_ext != NULL) {
        delete m_shot_ext;
        m_shot_ext = NULL;
    }

    ret = destroy();
    if (ret != NO_ERROR)
        CLOGE("destroy fail");
}

status_t ExynosCameraFrameFactoryPreview::create()
{
    Mutex::Autolock lock(ExynosCameraStreamMutex::getInstance()->getStreamMutex());
    CLOGI("");

    status_t ret = NO_ERROR;

    ret = ExynosCameraFrameFactoryBase::create();
    if (ret != NO_ERROR) {
        CLOGE("Pipe create fail, ret(%d)", ret);
        return ret;
    }

    /* EOS */
    ret = m_pipes[PIPE_3AA]->setControl(V4L2_CID_IS_END_OF_STREAM, 1);
    if (ret != NO_ERROR) {
        CLOGE("PIPE_%d V4L2_CID_IS_END_OF_STREAM fail, ret(%d)", PIPE_3AA, ret);
        /* TODO: exception handling */
        return INVALID_OPERATION;
    }

    /* s_ctrl HAL version for selecting dvfs table */
    ret = m_pipes[PIPE_3AA]->setControl(V4L2_CID_IS_HAL_VERSION, IS_HAL_VER_3_2);
    if (ret < 0)
        CLOGW("V4L2_CID_IS_HAL_VERSION is fail");

    ret = m_transitState(FRAME_FACTORY_STATE_CREATE);

    return ret;
}

status_t ExynosCameraFrameFactoryPreview::postcreate(void)
{
    CLOGI("");
    status_t ret = NO_ERROR;

    ret = ExynosCameraFrameFactoryBase::postcreate();
    if (ret != NO_ERROR) {
        CLOGE("Pipe create fail, ret(%d)", ret);
        return ret;
    }

    /* EOS */
    ret = m_pipes[PIPE_3AA]->setControl(V4L2_CID_IS_END_OF_STREAM, 1);
    if (ret != NO_ERROR) {
        CLOGE("PIPE_%d V4L2_CID_IS_END_OF_STREAM fail, ret(%d)", PIPE_3AA, ret);
        /* TODO: exception handling */
        return INVALID_OPERATION;
    }

    /* s_ctrl HAL version for selecting dvfs table */
    ret = m_pipes[PIPE_3AA]->setControl(V4L2_CID_IS_HAL_VERSION, IS_HAL_VER_3_2);
    if (ret < 0)
        CLOGW("WARN(%s): V4L2_CID_IS_HAL_VERSION is fail", __FUNCTION__);

    ret = m_transitState(FRAME_FACTORY_STATE_CREATE);

    return ret;
}

status_t ExynosCameraFrameFactoryPreview::initPipes(void)
{
    CLOGI("");

    status_t ret = NO_ERROR;

    int hwSensorW = 0, hwSensorH = 0;
    uint32_t minFrameRate = 0, maxFrameRate = 0, sensorFrameRate = 0;

    m_parameters->getHwSensorSize(&hwSensorW, &hwSensorH);
    m_parameters->getPreviewFpsRange(&minFrameRate, &maxFrameRate);
    sensorFrameRate = maxFrameRate;

    /* setDeviceInfo does changing path */
    ret = m_setupConfig();
    if (ret != NO_ERROR) {
        CLOGE("m_setupConfig() fail");
        return ret;
    }

    /*
     * we must set flite'setupPipe on 3aa_pipe.
     * then, setControl/getControl about BNS size
     */
    ret = m_initPipes(sensorFrameRate);
    if (ret != NO_ERROR) {
        CLOGE("m_initPipes() fail");
        return ret;
    }

    ret = m_initFlitePipe(hwSensorW, hwSensorH, sensorFrameRate);
    if (ret != NO_ERROR) {
        CLOGE("m_initFlitePipe() fail");
        return ret;
    }

    m_frameCount = 0;

    ret = m_transitState(FRAME_FACTORY_STATE_INIT);

    return ret;
}

status_t ExynosCameraFrameFactoryPreview::mapBuffers(void)
{
    status_t ret = NO_ERROR;

    ret = m_pipes[PIPE_3AA]->setMapBuffer();
    if (ret != NO_ERROR) {
        CLOGE("3AA mapBuffer fail, ret(%d)", ret);
        /* TODO: exception handling */
        return INVALID_OPERATION;
    }

    if (m_flag3aaIspOTF == HW_CONNECTION_MODE_M2M) {
        ret = m_pipes[PIPE_ISP]->setMapBuffer();
        if (ret != NO_ERROR) {
            CLOGE("ISP mapBuffer fail, ret(%d)", ret);
            /* TODO: exception handling */
            return INVALID_OPERATION;
        }
    }

    if (m_flagIspMcscOTF == HW_CONNECTION_MODE_M2M) {
        ret = m_pipes[PIPE_MCSC]->setMapBuffer();
        if (ret != NO_ERROR) {
            CLOGE("MCSC mapBuffer fail, ret(%d)", ret);
            /* TODO: exception handling */
            return INVALID_OPERATION;
        }
    }

    if (m_flagMcscVraOTF == HW_CONNECTION_MODE_M2M) {
        ret = m_pipes[PIPE_VRA]->setMapBuffer();
        if (ret != NO_ERROR) {
            CLOGE("MCSC mapBuffer fail, ret(%d)", ret);
            /* TODO: exception handling */
            return INVALID_OPERATION;
        }
    }

    CLOGI("Map buffer Success!");

    return NO_ERROR;
}

status_t ExynosCameraFrameFactoryPreview::preparePipes(void)
{
    status_t ret = NO_ERROR;

    /* NOTE: Prepare for 3AA is moved after ISP stream on */
    /* we must not qbuf before stream on, when sensor group. */
    return NO_ERROR;
}

status_t ExynosCameraFrameFactoryPreview::startInitialThreads(void)
{
    status_t ret = NO_ERROR;

    CLOGI("start pre-ordered initial pipe thread");

    if (m_sensorStandby == false) {
        if (m_request[PIPE_VC0]
                && m_flagFlite3aaOTF == HW_CONNECTION_MODE_M2M) {
            ret = startThread(PIPE_FLITE);
            if (ret != NO_ERROR)
                return ret;
        }

        ret = startThread(PIPE_3AA);
        if (ret != NO_ERROR)
            return ret;
    }

    if (m_flag3aaIspOTF == HW_CONNECTION_MODE_M2M) {
        ret = startThread(PIPE_ISP);
        if (ret != NO_ERROR)
            return ret;
    }

    if (m_flagIspMcscOTF == HW_CONNECTION_MODE_M2M) {
        ret = startThread(PIPE_MCSC);
        if (ret != NO_ERROR)
            return ret;
    }

    if (m_flagMcscVraOTF == HW_CONNECTION_MODE_M2M) {
        ret = startThread(PIPE_VRA);
        if (ret != NO_ERROR)
            return ret;
    }

    return NO_ERROR;
}

status_t ExynosCameraFrameFactoryPreview::setStopFlag(void)
{
    CLOGI("");

    status_t ret = NO_ERROR;

    if (m_flagFlite3aaOTF == HW_CONNECTION_MODE_M2M) {
        ret = m_pipes[PIPE_FLITE]->setStopFlag();
    }

    if (m_pipes[PIPE_3AA]->flagStart() == true)
        ret = m_pipes[PIPE_3AA]->setStopFlag();

    if (m_pipes[PIPE_ISP]->flagStart() == true)
        ret = m_pipes[PIPE_ISP]->setStopFlag();

    if (m_pipes[PIPE_DCP]->flagStart() == true)
        ret = m_pipes[PIPE_DCP]->setStopFlag();

    if (m_pipes[PIPE_MCSC]->flagStart() == true)
        ret = m_pipes[PIPE_MCSC]->setStopFlag();

    if (m_pipes[PIPE_VRA]->flagStart() == true)
        ret = m_pipes[PIPE_VRA]->setStopFlag();

    return NO_ERROR;
}

status_t ExynosCameraFrameFactoryPreview::sensorStandby(bool flagStandby)
{
    Mutex::Autolock lock(m_sensorStandbyLock);

    ExynosCameraAutoTimer autoTimer(__FUNCTION__);

    status_t ret = NO_ERROR;

    int pipeId = -1;

    if (m_sensorStandby == flagStandby) {
        CLOGI("already sensor standby(%d)", flagStandby);
        return ret;
    }

    m_sensorStandby = flagStandby;

    if (m_parameters->getHwConnectionMode(PIPE_FLITE, PIPE_3AA) == true) {
        pipeId = PIPE_3AA;
    } else {
        pipeId = PIPE_FLITE;
    }

    ret = m_pipes[pipeId]->sensorStandby(flagStandby);
    if (ret != NO_ERROR) {
        CLOGE("Sensor standby(%s) fail! ret(%d)",
                (flagStandby?"On":"Off"), ret);
    }

    CLOGI("Sensor Standby(%s) End", (flagStandby?"On":"Off"));

    return ret;
}

status_t ExynosCameraFrameFactoryPreview::m_setDeviceInfo(void)
{
    CLOGI("");

    int pipeId = -1;
    int node3aa = -1, node3ac = -1, node3ap = -1;
    int nodeIsp = -1, nodeIspc = -1, nodeIspp = -1;
    int nodeMcsc = -1, nodeMcscp0 = -1, nodeMcscp1 = -1, nodeMcscp2 = -1, nodeMcscpDs = -1;
    int nodeVra = -1, nodeMe = -1;
    int previousPipeId = -1;
    int vraSrcPipeId = -1;
    enum NODE_TYPE nodeType = INVALID_NODE;
    bool flagStreamLeader = true;

    m_initDeviceInfo(PIPE_FLITE);
    m_initDeviceInfo(PIPE_3AA);
    m_initDeviceInfo(PIPE_ISP);
    m_initDeviceInfo(PIPE_MCSC);
    m_initDeviceInfo(PIPE_VRA);

#ifdef USE_DUAL_CAMERA
    if (m_parameters->getDualMode() == true
        && (getCameraId() == CAMERA_ID_BACK_1 || getCameraId() == CAMERA_ID_FRONT_1)) {
        node3aa = FIMC_IS_VIDEO_31S_NUM;
        node3ac = FIMC_IS_VIDEO_31C_NUM;
        node3ap = FIMC_IS_VIDEO_31P_NUM;
    } else
#endif
    {
#if defined(FRONT_CAMERA_3AA_NUM)
        if (getCameraId() == CAMERA_ID_FRONT_0) {
            if (FRONT_CAMERA_3AA_NUM == FIMC_IS_VIDEO_30S_NUM) {
                node3aa = FIMC_IS_VIDEO_30S_NUM;
                node3ac = FIMC_IS_VIDEO_30C_NUM;
                node3ap = FIMC_IS_VIDEO_30P_NUM;
            } else {
                node3aa = FIMC_IS_VIDEO_31S_NUM;
                node3ac = FIMC_IS_VIDEO_31C_NUM;
                node3ap = FIMC_IS_VIDEO_31P_NUM;
            }
        } else
#endif
        {
            /* default single path */
            node3aa = FIMC_IS_VIDEO_30S_NUM;
            node3ac = FIMC_IS_VIDEO_30C_NUM;
            node3ap = FIMC_IS_VIDEO_30P_NUM;
        }
    }
    nodeIsp = FIMC_IS_VIDEO_I0S_NUM;
    nodeIspc = FIMC_IS_VIDEO_I0C_NUM;
    nodeIspp = FIMC_IS_VIDEO_I0P_NUM;
    nodeMe = FIMC_IS_VIDEO_ME0C_NUM;
    nodeMcsc = FIMC_IS_VIDEO_M0S_NUM;
    nodeMcscp0 = FIMC_IS_VIDEO_M0P_NUM;
    nodeMcscp1 = FIMC_IS_VIDEO_M1P_NUM;
    nodeMcscp2 = FIMC_IS_VIDEO_M2P_NUM;
    nodeVra = FIMC_IS_VIDEO_VRA_NUM;

    switch (m_parameters->getNumOfMcscOutputPorts()) {
    case 5:
        nodeMcscpDs = FIMC_IS_VIDEO_M5P_NUM;
        break;
    case 3:
        nodeMcscpDs = FIMC_IS_VIDEO_M3P_NUM;
        break;
    default:
        CLOGE("invalid output port(%d)", m_parameters->getNumOfMcscOutputPorts());
        break;
    }

    /*
     * FLITE
     */
    if (m_flagFlite3aaOTF == HW_CONNECTION_MODE_M2M) {
        pipeId = PIPE_FLITE;
    } else {
        pipeId = PIPE_3AA;
    }

    /* FLITE */
    nodeType = getNodeType(PIPE_FLITE);
    m_deviceInfo[pipeId].pipeId[nodeType]  = PIPE_FLITE;
    m_deviceInfo[pipeId].nodeNum[nodeType] = getFliteNodenum(m_cameraId);
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "FLITE", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[nodeType], false, flagStreamLeader, m_flagReprocessing);

    /* Other nodes is not stream leader */
    flagStreamLeader = false;

    /* VC0 for bayer */
    nodeType = getNodeType(PIPE_VC0);
    m_deviceInfo[pipeId].pipeId[nodeType]  = PIPE_VC0;
    m_deviceInfo[pipeId].nodeNum[nodeType] = getFliteCaptureNodenum(m_cameraId, m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_FLITE)]);
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "BAYER", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_FLITE)], false, flagStreamLeader, m_flagReprocessing);

#ifdef SUPPORT_DEPTH_MAP
    /* VC1 for depth */
    if (m_parameters->isDepthMapSupported()) {
        nodeType = getNodeType(PIPE_VC1);
        m_deviceInfo[pipeId].pipeId[nodeType]  = PIPE_VC1;
        m_deviceInfo[pipeId].nodeNum[nodeType] = getDepthVcNodeNum(m_cameraId);
        m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
        strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "DEPTH", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
        m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_FLITE)], false, flagStreamLeader, m_flagReprocessing);
    }
#endif // SUPPORT_DEPTH_MAP

    /*
     * 3AA
     */
    previousPipeId = pipeId;
    pipeId = PIPE_3AA;

    /* 3AS */
    nodeType = getNodeType(PIPE_3AA);
    m_deviceInfo[pipeId].pipeId[nodeType]  = PIPE_3AA;
    m_deviceInfo[pipeId].nodeNum[nodeType] = node3aa;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "3AA_OUTPUT", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[previousPipeId].nodeNum[getNodeType(PIPE_VC0)], m_flagFlite3aaOTF, flagStreamLeader, m_flagReprocessing);

    /* 3AC */
    nodeType = getNodeType(PIPE_3AC);
    m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_3AC;
    m_deviceInfo[pipeId].nodeNum[nodeType] = node3ac;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "3AA_CAPTURE", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_3AA)], true, flagStreamLeader, m_flagReprocessing);

    /* 3AP */
    nodeType = getNodeType(PIPE_3AP);
    m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_3AP;
    m_deviceInfo[pipeId].nodeNum[nodeType] = node3ap;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "3AA_PREVIEW", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_3AA)], true, flagStreamLeader, m_flagReprocessing);


    /*
     * ISP
     */
    previousPipeId = pipeId;

    if (m_flag3aaIspOTF == HW_CONNECTION_MODE_M2M) {
        pipeId = PIPE_ISP;
    }

    /* ISPS */
    nodeType = getNodeType(PIPE_ISP);
    m_deviceInfo[pipeId].pipeId[nodeType]  = PIPE_ISP;
    m_deviceInfo[pipeId].nodeNum[nodeType] = nodeIsp;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "ISP_OUTPUT", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[previousPipeId].nodeNum[getNodeType(PIPE_3AP)], m_flag3aaIspOTF, flagStreamLeader, m_flagReprocessing);

    /* ISPC */
    nodeType = getNodeType(PIPE_ISPC);
    m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_ISPC;
    m_deviceInfo[pipeId].nodeNum[nodeType] = nodeIspc;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "ISP_CAPTURE", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_ISP)], true, flagStreamLeader, m_flagReprocessing);

    /* ISPP */
    nodeType = getNodeType(PIPE_ISPP);
    m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_ISPP;
    m_deviceInfo[pipeId].nodeNum[nodeType] = nodeIspp;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "ISP_PREVIEW", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_ISP)], true, flagStreamLeader, m_flagReprocessing);

#ifdef SUPPORT_ME
    /* ME */
    nodeType = getNodeType(PIPE_ME);
    m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_ME;
    m_deviceInfo[pipeId].nodeNum[nodeType] = nodeMe;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "ME_PREVIEW", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_ME)], true, flagStreamLeader, m_flagReprocessing);
#endif

    /*
     * MCSC
     */
    previousPipeId = pipeId;

    if (m_flagIspMcscOTF == HW_CONNECTION_MODE_M2M) {
        pipeId = PIPE_MCSC;
    }

    /* MCSC */
    nodeType = getNodeType(PIPE_MCSC);
    m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_MCSC;
    m_deviceInfo[pipeId].nodeNum[nodeType] = nodeMcsc;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "MCSC_OUTPUT", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[previousPipeId].nodeNum[getNodeType(PIPE_ISPC)], m_flagIspMcscOTF, flagStreamLeader, m_flagReprocessing);

    /* MCSC0 */
    nodeType = getNodeType(PIPE_MCSC0);
    m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_MCSC0;
    m_deviceInfo[pipeId].nodeNum[nodeType] = nodeMcscp0;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "MCSC_PREVIEW", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_MCSC)], true, flagStreamLeader, m_flagReprocessing);

    /* MCSC1 */
    nodeType = getNodeType(PIPE_MCSC1);
    m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_MCSC1;
    m_deviceInfo[pipeId].nodeNum[nodeType] = nodeMcscp1;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "MCSC_PREVIEW_CALLBACK", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_MCSC)], true, flagStreamLeader, m_flagReprocessing);

    /* MCSC2 */
    nodeType = getNodeType(PIPE_MCSC2);
    m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_MCSC2;
    m_deviceInfo[pipeId].nodeNum[nodeType] = nodeMcscp2;
    m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
    strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "MCSC_RECORDING", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
    m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_MCSC)], true, flagStreamLeader, m_flagReprocessing);

    if (m_flagMcscVraOTF == HW_CONNECTION_MODE_M2M) {
        /* MCSC5 */
        nodeType = getNodeType(PIPE_MCSC5);
        m_deviceInfo[pipeId].pipeId[nodeType] = PIPE_MCSC5;
        m_deviceInfo[pipeId].nodeNum[nodeType] = nodeMcscpDs;
        m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
        strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "MCSC_DS", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
        m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[pipeId].nodeNum[getNodeType(PIPE_MCSC)], true, flagStreamLeader, m_flagReprocessing);

        /*
         * VRA
         */
        previousPipeId = pipeId;
        vraSrcPipeId = PIPE_MCSC5;
        pipeId = PIPE_VRA;

        nodeType = getNodeType(PIPE_VRA);
        m_deviceInfo[pipeId].pipeId[nodeType]  = PIPE_VRA;
        m_deviceInfo[pipeId].nodeNum[nodeType] = nodeVra;
        m_deviceInfo[pipeId].bufferManagerType[nodeType] = BUFFER_MANAGER_ION_TYPE;
        strncpy(m_deviceInfo[pipeId].nodeName[nodeType], "VRA_OUTPUT", EXYNOS_CAMERA_NAME_STR_SIZE - 1);
        m_sensorIds[pipeId][nodeType] = m_getSensorId(m_deviceInfo[previousPipeId].nodeNum[getNodeType(vraSrcPipeId)], m_flagMcscVraOTF, flagStreamLeader, m_flagReprocessing);
    }

    return NO_ERROR;
}

status_t ExynosCameraFrameFactoryPreview::m_initPipes(uint32_t frameRate)
{
    CLOGI("");

    status_t ret = NO_ERROR;
    camera_pipe_info_t pipeInfo[MAX_NODE];
    camera_pipe_info_t nullPipeInfo;

    int pipeId = -1;
    enum NODE_TYPE nodeType = INVALID_NODE;
    enum NODE_TYPE leaderNodeType = OUTPUT_NODE;

    int maxSensorW = 0, maxSensorH = 0, hwSensorW = 0, hwSensorH = 0;
    int yuvWidth[ExynosCameraParameters::YUV_MAX] = {0};
    int yuvHeight[ExynosCameraParameters::YUV_MAX] = {0};
    int yuvFormat[ExynosCameraParameters::YUV_MAX] = {0};
    int dsWidth = MAX_VRA_INPUT_WIDTH;
    int dsHeight = MAX_VRA_INPUT_HEIGHT;
    int dsFormat = m_parameters->getHwVraInputFormat();;
    int yuvBufferCnt[ExynosCameraParameters::YUV_MAX] = {0};
    int stride = 0;
    int bayerFormat = m_parameters->getBayerFormat(PIPE_3AA);
    int hwVdisformat = m_parameters->getHWVdisFormat();
#ifdef SUPPORT_ME
    int hwMeFormat = m_parameters->getMeFormat();
#endif
    int perFramePos = 0;
    int yuvIndex = -1;
    int captureBayerCount;
    struct ExynosConfigInfo *config = m_parameters->getConfig();
    ExynosRect bnsSize;
    ExynosRect bcropSize;
    ExynosRect bdsSize;
    ExynosRect tempRect;

#ifdef DEBUG_RAWDUMP
    if (m_parameters->checkBayerDumpEnable()) {
        bayerFormat = CAMERA_DUMP_BAYER_FORMAT;
    }
#endif

    m_parameters->getMaxSensorSize(&maxSensorW, &maxSensorH);
    m_parameters->getHwSensorSize(&hwSensorW, &hwSensorH);

    m_parameters->getPreviewBayerCropSize(&bnsSize, &bcropSize, false);
    m_parameters->getPreviewBdsSize(&bdsSize, false);

    CLOGI("MaxSensorSize %dx%d bayerFormat %x",
             maxSensorW, maxSensorH, bayerFormat);
    CLOGI("BnsSize %dx%d BcropSize %dx%d BdsSize %dx%d",
            bnsSize.w, bnsSize.h, bcropSize.w, bcropSize.h, bdsSize.w, bdsSize.h);
    CLOGI("DS Size %dx%d Format %x Buffer count %d",
            dsWidth, dsHeight, dsFormat, config->current->bufInfo.num_vra_buffers);

    for (int i = ExynosCameraParameters::YUV_0; i < ExynosCameraParameters::YUV_MAX; i++) {
        m_parameters->getHwYuvSize(&yuvWidth[i], &yuvHeight[i], i);
        yuvFormat[i] = m_parameters->getYuvFormat(i);
        yuvBufferCnt[i] = m_parameters->getYuvBufferCount(i);

        CLOGI("YUV[%d] Size %dx%d Format %x Buffer count %d",
                i, yuvWidth[i], yuvHeight[i], yuvFormat[i], yuvBufferCnt[i]);
    }

    /*
     * FLITE
     */
    if (m_flagFlite3aaOTF == HW_CONNECTION_MODE_M2M) {
        pipeId = PIPE_FLITE;
    } else {
        pipeId = PIPE_3AA;
    }

    /* setParam for Frame rate : must after setInput on Flite */
    struct v4l2_streamparm streamParam;
    memset(&streamParam, 0x0, sizeof(v4l2_streamparm));

    streamParam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    streamParam.parm.capture.timeperframe.numerator   = 1;
    streamParam.parm.capture.timeperframe.denominator = frameRate;
    CLOGI("Set framerate (denominator=%d)", frameRate);

    ret = setParam(&streamParam, pipeId);
    if (ret != NO_ERROR) {
        CLOGE("FLITE setParam(frameRate(%d), pipeId(%d)) fail", frameRate, pipeId);
        return INVALID_OPERATION;
    }

    ret = m_setSensorSize(pipeId, hwSensorW, hwSensorH);
    if (ret != NO_ERROR) {
        CLOGE("m_setSensorSize(pipeId(%d), hwSensorW(%d), hwSensorH(%d)) fail", pipeId, hwSensorW, hwSensorH);
        return ret;
    }

    /* FLITE */
    nodeType = getNodeType(PIPE_FLITE);

    /* set v4l2 buffer size */
    tempRect.fullW = 32;
    tempRect.fullH = 64;
    tempRect.colorFormat = bayerFormat;

    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_3aa_buffers;

    /* Set output node default info */
    SET_OUTPUT_DEVICE_BASIC_INFO(PERFRAME_INFO_FLITE);

    /* BAYER */
    nodeType = getNodeType(PIPE_VC0);
    perFramePos = PERFRAME_BACK_VC0_POS;

    /* set v4l2 buffer size */
    tempRect.fullW = hwSensorW;
    tempRect.fullH = hwSensorH;
    tempRect.colorFormat = bayerFormat;

    /* set v4l2 video node bytes per plane */
    pipeInfo[nodeType].bytesPerPlane[0] = getBayerLineSize(tempRect.fullW, bayerFormat);

    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_bayer_buffers;

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();

#ifdef SUPPORT_DEPTH_MAP
    if (m_parameters->isDepthMapSupported()) {
        /* Depth Map Configuration */
        int depthMapW = 0, depthMapH = 0;
        int depthMapFormat = DEPTH_MAP_FORMAT;

        ret = m_parameters->getDepthMapSize(&depthMapW, &depthMapH);
        if (ret != NO_ERROR) {
            CLOGE("Failed to getDepthMapSize");
            return ret;
        }

        CLOGI("DepthMapSize %dx%d", depthMapW, depthMapH);

        tempRect.fullW = depthMapW;
        tempRect.fullH = depthMapH;
        tempRect.colorFormat = depthMapFormat;

        nodeType = getNodeType(PIPE_VC1);
        pipeInfo[nodeType].bytesPerPlane[0] = getBayerLineSize(tempRect.fullW, tempRect.colorFormat);
        pipeInfo[nodeType].bufInfo.count = NUM_DEPTHMAP_BUFFERS;

        SET_CAPTURE_DEVICE_BASIC_INFO();
    }
#endif

    /* setup pipe info to FLITE pipe */
    if (m_flagFlite3aaOTF == HW_CONNECTION_MODE_M2M) {
        ret = m_pipes[pipeId]->setupPipe(pipeInfo, m_sensorIds[pipeId]);
        if (ret != NO_ERROR) {
            CLOGE("FLITE setupPipe fail, ret(%d)", ret);
            /* TODO: exception handling */
            return INVALID_OPERATION;
        }

        /* clear pipeInfo for next setupPipe */
        for (int i = 0; i < MAX_NODE; i++)
            pipeInfo[i] = nullPipeInfo;
    }

    /*
     * 3AA
     */
    pipeId = PIPE_3AA;

    /* 3AS */
    nodeType = getNodeType(PIPE_3AA);
    bayerFormat = m_parameters->getBayerFormat(PIPE_3AA);

    if (m_flagFlite3aaOTF == HW_CONNECTION_MODE_OTF) {
        /* set v4l2 buffer size */
        tempRect.fullW = 32;
        tempRect.fullH = 64;
        tempRect.colorFormat = bayerFormat;

        /* set v4l2 video node buffer count */
        pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_3aa_buffers;
    } else if (m_flagFlite3aaOTF == HW_CONNECTION_MODE_M2M) {
        /* set v4l2 buffer size */
        tempRect.fullW = hwSensorW;
        tempRect.fullH = hwSensorH;
        tempRect.colorFormat = bayerFormat;

        /* set v4l2 video node bytes per plane */
        pipeInfo[nodeType].bytesPerPlane[0] = getBayerLineSize(tempRect.fullW, bayerFormat);

#if 0
        switch (bayerFormat) {
        case V4L2_PIX_FMT_SBGGR16:
            pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 2), CAMERA_16PX_ALIGN);
            break;
        case V4L2_PIX_FMT_SBGGR12:
            pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 3 / 2), CAMERA_16PX_ALIGN);
            break;
        case V4L2_PIX_FMT_SBGGR10:
            pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 5 / 4), CAMERA_16PX_ALIGN);
            break;
        default:
            CLOGW("Invalid bayer format(%d)", bayerFormat);
            pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 2), CAMERA_16PX_ALIGN);
            break;
        }
#endif
        /* set v4l2 video node buffer count */
        pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_bayer_buffers;
    }

    /* Set output node default info */
    SET_OUTPUT_DEVICE_BASIC_INFO(PERFRAME_INFO_3AA);

    /* 3AC */
    nodeType = getNodeType(PIPE_3AC);
    perFramePos = PERFRAME_BACK_3AC_POS;
    bayerFormat = m_parameters->getBayerFormat(PIPE_3AC);

    /* set v4l2 buffer size */
    tempRect.fullW = bcropSize.w;
    tempRect.fullH = bcropSize.h;
    tempRect.colorFormat = bayerFormat;

    pipeInfo[nodeType].bytesPerPlane[0] = getBayerLineSize(tempRect.fullW, bayerFormat);
#if 0
    /* set v4l2 video node bytes per plane */
    switch (bayerFormat) {
    case V4L2_PIX_FMT_SBGGR16:
        pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 2), CAMERA_16PX_ALIGN);
        break;
    case V4L2_PIX_FMT_SBGGR12:
        pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 3 / 2), CAMERA_16PX_ALIGN);
        break;
    case V4L2_PIX_FMT_SBGGR10:
        pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 5 / 4), CAMERA_16PX_ALIGN);
        break;
    default:
        CLOGW("Invalid bayer format(%d)", bayerFormat);
        pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 2), CAMERA_16PX_ALIGN);
        break;
    }
#endif
    /* set v4l2 video node buffer count */
    switch(m_parameters->getReprocessingBayerMode()) {
        case REPROCESSING_BAYER_MODE_PURE_ALWAYS_ON:
        case REPROCESSING_BAYER_MODE_PURE_DYNAMIC:
        case REPROCESSING_BAYER_MODE_NONE:
            pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_3aa_buffers;
            break;
        case REPROCESSING_BAYER_MODE_DIRTY_ALWAYS_ON:
        case REPROCESSING_BAYER_MODE_DIRTY_DYNAMIC:
            if (m_parameters->isSupportZSLInput()) {
                pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_bayer_buffers;
            } else {
                pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_sensor_buffers;
            }
            break;
        default:
            CLOGE("Invalid reprocessing mode(%d)", m_parameters->getReprocessingBayerMode());
    }

    captureBayerCount = pipeInfo[nodeType].bufInfo.count;

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();

    /* 3AP */
    nodeType = getNodeType(PIPE_3AP);
    perFramePos = PERFRAME_BACK_3AP_POS;
    bayerFormat = m_parameters->getBayerFormat(PIPE_3AP);

    /* set v4l2 buffer size */
    tempRect.fullW = bdsSize.w;
    tempRect.fullH = bdsSize.h;
    tempRect.colorFormat = bayerFormat;

    /* set v4l2 video node bytes per plane */
    pipeInfo[nodeType].bytesPerPlane[0] = getBayerLineSize(tempRect.fullW, bayerFormat);
#if 0
    switch (bayerFormat) {
    case V4L2_PIX_FMT_SBGGR16:
        pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 2), CAMERA_16PX_ALIGN);
        break;
    case V4L2_PIX_FMT_SBGGR12:
        pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 3 / 2), CAMERA_16PX_ALIGN);
        break;
    case V4L2_PIX_FMT_SBGGR10:
        pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 5 / 4), CAMERA_16PX_ALIGN);
        break;
    default:
        CLOGW("Invalid bayer format(%d)", bayerFormat);
        pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 2), CAMERA_16PX_ALIGN);
        break;
    }
#endif
    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_3aa_buffers;

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();

    /* setup pipe info to 3AA pipe */
    if (m_flag3aaIspOTF == HW_CONNECTION_MODE_M2M) {
        ret = m_pipes[pipeId]->setupPipe(pipeInfo, m_sensorIds[pipeId]);
        if (ret != NO_ERROR) {
            CLOGE("3AA setupPipe fail, ret(%d)", ret);
            /* TODO: exception handling */
            return INVALID_OPERATION;
        }

        /* clear pipeInfo for next setupPipe */
        for (int i = 0; i < MAX_NODE; i++)
            pipeInfo[i] = nullPipeInfo;
    }

    /*
     * ISP
     */

    /* ISPS */
    if (m_flag3aaIspOTF == HW_CONNECTION_MODE_M2M) {
        pipeId = PIPE_ISP;
        nodeType = getNodeType(PIPE_ISP);
        bayerFormat = m_parameters->getBayerFormat(PIPE_ISP);

        /* set v4l2 buffer size */
        tempRect.fullW = bdsSize.w;
        tempRect.fullH = bdsSize.h;
        tempRect.colorFormat = bayerFormat;

        /* set v4l2 video node bytes per plane */
        pipeInfo[nodeType].bytesPerPlane[0] = getBayerLineSize(tempRect.fullW, bayerFormat);
#if 0
        switch (bayerFormat) {
        case V4L2_PIX_FMT_SBGGR16:
            pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 2), CAMERA_16PX_ALIGN);
            break;
        case V4L2_PIX_FMT_SBGGR12:
            pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 3 / 2), CAMERA_16PX_ALIGN);
            break;
        case V4L2_PIX_FMT_SBGGR10:
            pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 5 / 4), CAMERA_16PX_ALIGN);
            break;
        default:
            CLOGW("Invalid bayer format(%d)", bayerFormat);
            pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP((tempRect.fullW * 2), CAMERA_16PX_ALIGN);
            break;
        }
#endif
        /* set v4l2 video node buffer count */
        pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_3aa_buffers;

        /* Set output node default info */
        SET_OUTPUT_DEVICE_BASIC_INFO(PERFRAME_INFO_ISP);
    }

    /* ISPC */
    nodeType = getNodeType(PIPE_ISPC);
    perFramePos = PERFRAME_BACK_ISPC_POS;

    /* set v4l2 buffer size */
    tempRect.fullW = bdsSize.w;
    tempRect.fullH = bdsSize.h;
    tempRect.colorFormat = hwVdisformat;
    pipeInfo[nodeType].pixelSize = CAMERA_PIXEL_SIZE_PACKED_10BIT;

    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_hwdis_buffers;;

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();

    /* ISPP */
    nodeType = getNodeType(PIPE_ISPP);
    perFramePos = PERFRAME_BACK_ISPP_POS;

    /* set v4l2 buffer size */
    tempRect.fullW = bdsSize.w;
    tempRect.fullH = bdsSize.h;
    tempRect.colorFormat = hwVdisformat;

    pipeInfo[nodeType].pixelSize = CAMERA_PIXEL_SIZE_PACKED_10BIT;

    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_hwdis_buffers;;

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();

#ifdef SUPPORT_ME
    /* ME */
    nodeType = getNodeType(PIPE_ME);
    perFramePos = PERFRAME_BACK_ME_POS;

    /* set v4l2 buffer size */
    m_parameters->getMeSize(&tempRect.fullW, &tempRect.fullH);
    tempRect.colorFormat = hwMeFormat;

    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = captureBayerCount;

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();
#endif

    /* setup pipe info to ISP pipe */
    if (m_flagIspMcscOTF == HW_CONNECTION_MODE_M2M) {
        ret = m_pipes[pipeId]->setupPipe(pipeInfo, m_sensorIds[pipeId]);
        if (ret != NO_ERROR) {
            CLOGE("ISP setupPipe fail, ret(%d)", ret);
            /* TODO: exception handling */
            return INVALID_OPERATION;
        }

        /* clear pipeInfo for next setupPipe */
        for (int i = 0; i < MAX_NODE; i++)
            pipeInfo[i] = nullPipeInfo;
    }

    /*
     * MCSC
     */

    /* MCSC */
    if (m_flagIspMcscOTF == HW_CONNECTION_MODE_M2M) {
        pipeId = PIPE_MCSC;
        nodeType = getNodeType(PIPE_MCSC);

        /* set v4l2 buffer size */
        tempRect.fullW = bdsSize.w;
        tempRect.fullH = bdsSize.h;
        tempRect.colorFormat = hwVdisformat;

        /* set v4l2 video node bytes per plane */
        pipeInfo[nodeType].bytesPerPlane[0] = ROUND_UP(tempRect.fullW * 2, CAMERA_16PX_ALIGN);

        /* set v4l2 video node buffer count */
        pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_hwdis_buffers;

        /* Set output node default info */
        SET_OUTPUT_DEVICE_BASIC_INFO(PERFRAME_INFO_MCSC);
    }

    /* MCSC0 */
    nodeType = getNodeType(PIPE_MCSC0);
    perFramePos = PERFRAME_BACK_SCP_POS;
    yuvIndex = ExynosCameraParameters::YUV_0;
    m_parameters->setYuvOutPortId(PIPE_MCSC0, yuvIndex);

    /* set v4l2 buffer size */
    tempRect.fullW = yuvWidth[yuvIndex];
    tempRect.fullH = yuvHeight[yuvIndex];
    tempRect.colorFormat = yuvFormat[yuvIndex];

    /* set v4l2 video node bytes per plane */
#ifdef USE_BUFFER_WITH_STRIDE
    /* to use stride for preview buffer, set the bytesPerPlane */
    pipeInfo[nodeType].bytesPerPlane[0] = yuvWidth[yuvIndex];
#endif

    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = yuvBufferCnt[yuvIndex];

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();

    /* MCSC1 */
    nodeType = getNodeType(PIPE_MCSC1);
    perFramePos = PERFRAME_BACK_MCSC1_POS;
    yuvIndex = ExynosCameraParameters::YUV_1;
    m_parameters->setYuvOutPortId(PIPE_MCSC1, yuvIndex);

    /* set v4l2 buffer size */
    tempRect.fullW = yuvWidth[yuvIndex];
    tempRect.fullH = yuvHeight[yuvIndex];
    tempRect.colorFormat = yuvFormat[yuvIndex];

    /* set v4l2 video node bytes per plane */
#ifdef USE_BUFFER_WITH_STRIDE
    /* to use stride for preview buffer, set the bytesPerPlane */
    pipeInfo[nodeType].bytesPerPlane[0] = yuvWidth[yuvIndex];
#endif

    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = yuvBufferCnt[yuvIndex];

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();

    /* MCSC2 */
    nodeType = getNodeType(PIPE_MCSC2);
    perFramePos = PERFRAME_BACK_MCSC2_POS;
    yuvIndex = ExynosCameraParameters::YUV_2;
    m_parameters->setYuvOutPortId(PIPE_MCSC2, yuvIndex);

    /* set v4l2 buffer size */
    tempRect.fullW = yuvWidth[yuvIndex];
    tempRect.fullH = yuvHeight[yuvIndex];
    tempRect.colorFormat = yuvFormat[yuvIndex];

    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = yuvBufferCnt[yuvIndex];

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();

    /* MCSC5 */
    nodeType = getNodeType(PIPE_MCSC5);
    perFramePos = PERFRAME_BACK_MCSC5_POS;

    /* set v4l2 buffer size */
    tempRect.fullW = dsWidth;
    tempRect.fullH = dsHeight;
    tempRect.colorFormat = dsFormat;

    /* set v4l2 video node buffer count */
    pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_vra_buffers;

    /* Set capture node default info */
    SET_CAPTURE_DEVICE_BASIC_INFO();

    if (m_flagMcscVraOTF == HW_CONNECTION_MODE_M2M) {

        ret = m_pipes[pipeId]->setupPipe(pipeInfo, m_sensorIds[pipeId]);
        if (ret != NO_ERROR) {
            CLOGE("MCSC setupPipe fail, ret(%d)", ret);
            /* TODO: exception handling */
            return INVALID_OPERATION;
        }

        /* clear pipeInfo for next setupPipe */
        for (int i = 0; i < MAX_NODE; i++)
            pipeInfo[i] = nullPipeInfo;
    }


    /* VRA */
    if (m_flagMcscVraOTF == HW_CONNECTION_MODE_M2M) {
        pipeId = PIPE_VRA;
        nodeType = getNodeType(PIPE_VRA);

        /* set v4l2 buffer size */
        tempRect.fullW = dsWidth;
        tempRect.fullH = dsHeight;
        tempRect.colorFormat = dsFormat;

        /* set v4l2 video node buffer count */
        pipeInfo[nodeType].bufInfo.count = config->current->bufInfo.num_vra_buffers;

        /* Set output node default info */
        SET_OUTPUT_DEVICE_BASIC_INFO(PERFRAME_INFO_VRA);
    }

    ret = m_pipes[pipeId]->setupPipe(pipeInfo, m_sensorIds[pipeId]);
    if (ret != NO_ERROR) {
        CLOGE("MCSC setupPipe fail, ret(%d)", ret);
        /* TODO: exception handling */
        return INVALID_OPERATION;
    }

    return NO_ERROR;
}

status_t ExynosCameraFrameFactoryPreview::m_fillNodeGroupInfo(ExynosCameraFrameSP_sptr_t frame)
{
    camera2_node_group node_group_info_flite;
    camera2_node_group node_group_info_3aa;
    camera2_node_group node_group_info_isp;
    camera2_node_group node_group_info_mcsc;
    camera2_node_group node_group_info_vra;
    camera2_node_group *node_group_info_temp;

    float zoomRatio = m_parameters->getZoomRatio();
    int pipeId = -1;
    int nodePipeId = -1;
    uint32_t perframePosition = 0;

    int yuvFormat[ExynosCameraParameters::YUV_MAX] = {0};
    int yuvIndex = -1;

    for (int i = ExynosCameraParameters::YUV_0; i < ExynosCameraParameters::YUV_MAX; i++) {
        yuvFormat[i] = m_parameters->getYuvFormat(i);
    }

    memset(&node_group_info_flite, 0x0, sizeof(camera2_node_group));
    memset(&node_group_info_3aa, 0x0, sizeof(camera2_node_group));
    memset(&node_group_info_isp, 0x0, sizeof(camera2_node_group));
    memset(&node_group_info_mcsc, 0x0, sizeof(camera2_node_group));
    memset(&node_group_info_vra, 0x0, sizeof(camera2_node_group));

    if (m_flagFlite3aaOTF != HW_CONNECTION_MODE_M2M) {
        /* 3AA */
        pipeId = PIPE_3AA;
        perframePosition = 0;

        node_group_info_temp = &node_group_info_flite;
        node_group_info_temp->leader.request = 1;
        node_group_info_temp->leader.pixelformat = m_parameters->getBayerFormat(pipeId);

        nodePipeId = PIPE_VC0;
        node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
        node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
        node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getBayerFormat(nodePipeId);
        perframePosition++;

#ifdef SUPPORT_DEPTH_MAP
        /* VC1 for depth */
        if (m_parameters->getUseDepthMap() == true) {
            nodePipeId = PIPE_VC1;
            node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
            node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
            node_group_info_temp->capture[perframePosition].pixelformat = DEPTH_MAP_FORMAT;
            perframePosition++;
        }
#endif // SUPPORT_DEPTH_MAP

        nodePipeId = PIPE_3AC;
        node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
        node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
        node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getBayerFormat(nodePipeId);
        perframePosition++;

        nodePipeId = PIPE_3AP;
        node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
        node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
        node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getBayerFormat(nodePipeId);
        perframePosition++;
    } else {
        /* FLITE */
        pipeId = PIPE_FLITE;
        perframePosition = 0;

        node_group_info_temp = &node_group_info_flite;
        node_group_info_temp->leader.request = 1;
        node_group_info_temp->leader.pixelformat = m_parameters->getBayerFormat(pipeId);

        nodePipeId = PIPE_VC0;
        node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
        node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
        node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getBayerFormat(nodePipeId);
        perframePosition++;

#ifdef SUPPORT_DEPTH_MAP
        /* VC1 for depth */
        if (m_parameters->getUseDepthMap() == true) {
            nodePipeId = PIPE_VC1;
            node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
            node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
            node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getBayerFormat(nodePipeId);
            perframePosition++;
        }
#endif // SUPPORT_DEPTH_MAP

        /* 3AA */
        pipeId = PIPE_3AA;
        perframePosition = 0;

        node_group_info_temp = &node_group_info_3aa;
        node_group_info_temp->leader.request = 1;
        node_group_info_temp->leader.pixelformat = m_parameters->getBayerFormat(pipeId);

        nodePipeId = PIPE_3AC;
        node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
        node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
        node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getBayerFormat(nodePipeId);
        perframePosition++;

        nodePipeId = PIPE_3AP;
        node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
        node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
        node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getBayerFormat(nodePipeId);
        perframePosition++;
    }

    /* ISP */
    if (m_flag3aaIspOTF == HW_CONNECTION_MODE_M2M) {
        pipeId = PIPE_ISP;
        perframePosition = 0;
        node_group_info_temp = &node_group_info_isp;
        node_group_info_temp->leader.request = 1;
        node_group_info_temp->leader.pixelformat = m_parameters->getBayerFormat(pipeId);
    }

    nodePipeId = PIPE_ISPC;
    node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
    node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
    node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getHWVdisFormat();
    perframePosition++;

#ifdef SUPPORT_ME
    nodePipeId = PIPE_ME;
    node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
    node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
    node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getMeFormat();
    perframePosition++;
#endif

    /* MCSC */
    if (m_flagIspMcscOTF == HW_CONNECTION_MODE_M2M) {
        pipeId = PIPE_MCSC;
        perframePosition = 0;
        node_group_info_temp = &node_group_info_mcsc;
        node_group_info_temp->leader.request = 1;
        node_group_info_temp->leader.pixelformat = m_parameters->getHWVdisFormat();
    }

    nodePipeId = PIPE_MCSC0;
    node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
    node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
    yuvIndex = ExynosCameraParameters::YUV_0;
    node_group_info_temp->capture[perframePosition].pixelformat = yuvFormat[yuvIndex];
    perframePosition++;

    nodePipeId = PIPE_MCSC1;
    node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
    node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
    yuvIndex = ExynosCameraParameters::YUV_1;
    node_group_info_temp->capture[perframePosition].pixelformat = yuvFormat[yuvIndex];
    perframePosition++;

    nodePipeId = PIPE_MCSC2;
    node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
    node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
    yuvIndex = ExynosCameraParameters::YUV_2;
    node_group_info_temp->capture[perframePosition].pixelformat = yuvFormat[yuvIndex];
    perframePosition++;

    /* VRA */
    if (m_flagMcscVraOTF == HW_CONNECTION_MODE_M2M) {
        nodePipeId = PIPE_MCSC5;
        node_group_info_temp->capture[perframePosition].request = m_request[nodePipeId];
        node_group_info_temp->capture[perframePosition].vid = m_deviceInfo[pipeId].nodeNum[getNodeType(nodePipeId)] - FIMC_IS_VIDEO_BAS_NUM;
        node_group_info_temp->capture[perframePosition].pixelformat = m_parameters->getHwVraInputFormat();
        perframePosition++;

        pipeId = PIPE_VRA;
        perframePosition = 0;
        node_group_info_temp = &node_group_info_vra;
        node_group_info_temp->leader.request = 1;
        node_group_info_temp->leader.pixelformat = m_parameters->getHwVraInputFormat();
    }

    frame->setZoomRatio(zoomRatio);

    if (m_flagFlite3aaOTF == HW_CONNECTION_MODE_M2M) {
        updateNodeGroupInfo(
                PIPE_FLITE,
                m_parameters,
                &node_group_info_flite);
        frame->storeNodeGroupInfo(&node_group_info_flite, PERFRAME_INFO_FLITE);

        updateNodeGroupInfo(
                PIPE_3AA,
                m_parameters,
                &node_group_info_3aa);
        frame->storeNodeGroupInfo(&node_group_info_3aa, PERFRAME_INFO_3AA);
    } else {
        updateNodeGroupInfo(
                PIPE_3AA,
                m_parameters,
                &node_group_info_flite);
        frame->storeNodeGroupInfo(&node_group_info_flite, PERFRAME_INFO_FLITE);
    }

    if (m_flag3aaIspOTF == HW_CONNECTION_MODE_M2M) {
        updateNodeGroupInfo(
                PIPE_ISP,
                m_parameters,
                &node_group_info_isp);
        frame->storeNodeGroupInfo(&node_group_info_isp, PERFRAME_INFO_ISP);
    }

    if (m_flagIspMcscOTF == HW_CONNECTION_MODE_M2M) {
        updateNodeGroupInfo(
                PIPE_MCSC,
                m_parameters,
                &node_group_info_mcsc);
        frame->storeNodeGroupInfo(&node_group_info_mcsc, PERFRAME_INFO_MCSC);
    }

    if (m_flagMcscVraOTF == HW_CONNECTION_MODE_M2M) {
        updateNodeGroupInfo(
                PIPE_VRA,
                m_parameters,
                &node_group_info_vra);
        frame->storeNodeGroupInfo(&node_group_info_vra, PERFRAME_INFO_VRA);
    }

    return NO_ERROR;
}

void ExynosCameraFrameFactoryPreview::m_init(void)
{
    m_flagReprocessing = false;

    m_shot_ext = new struct camera2_shot_ext;
}

}; /* namespace android */

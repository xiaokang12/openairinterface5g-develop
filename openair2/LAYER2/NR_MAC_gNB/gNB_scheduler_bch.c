/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file gNB_scheduler_bch.c
 * \brief procedures related to eNB for the BCH transport channel
 * \author  Navid Nikaein and Raymond Knopp, WEI-TAI CHEN
 * \date 2010 - 2014, 2018
 * \email: navid.nikaein@eurecom.fr, kroempa@gmail.com
 * \version 1.0
 * \company Eurecom, NTUST
 * @ingroup _mac

 */

#include "GNB_APP/RRC_nr_paramsvalues.h"
#include "assertions.h"
#include "NR_MAC_gNB/nr_mac_gNB.h"
#include "NR_MAC_gNB/mac_proto.h"
#include "NR_MAC_COMMON/nr_mac_extern.h"
#include "common/utils/LOG/log.h"
#include "common/utils/LOG/vcd_signal_dumper.h"
#include "UTIL/OPT/opt.h"
#include "OCG.h"
#include "RRC/NR/nr_rrc_extern.h"
#include "common/utils/nr/nr_common.h"


#include "pdcp.h"

#define ENABLE_MAC_PAYLOAD_DEBUG
#define DEBUG_eNB_SCHEDULER 1

#include "common/ran_context.h"

#include "executables/softmodem-common.h"

extern RAN_CONTEXT_t RC;


uint16_t get_ssboffset_pointa(NR_ServingCellConfigCommon_t *scc,const long band) {

  int ratio;
  switch (*scc->ssbSubcarrierSpacing) {
    case NR_SubcarrierSpacing_kHz15:
      AssertFatal(band <= 79,
                  "Band %ld is not possible for SSB with 15 kHz SCS\n",
                  band);
      if (band < 77)  // below 3GHz
        ratio = 3;    // NRARFCN step is 5 kHz
      else
        ratio = 1;  // NRARFCN step is 15 kHz
      break;
    case NR_SubcarrierSpacing_kHz30:
      AssertFatal(band <= 79,
                  "Band %ld is not possible for SSB with 15 kHz SCS\n",
                  band);
      if (band < 77)  // below 3GHz
        ratio = 6;    // NRARFCN step is 5 kHz
      else
        ratio = 2;  // NRARFCN step is 15 kHz
      break;
    case NR_SubcarrierSpacing_kHz120:
      AssertFatal(band >= 257,
                  "Band %ld is not possible for SSB with 120 kHz SCS\n",
                  band);
      ratio = 2;  // NRARFCN step is 15 kHz
      break;
    case NR_SubcarrierSpacing_kHz240:
      AssertFatal(band >= 257,
                  "Band %ld is not possible for SSB with 240 kHz SCS\n",
                  band);
      ratio = 4;  // NRARFCN step is 15 kHz
      break;
    default:
      AssertFatal(1 == 0, "SCS %ld not allowed for SSB \n",
                  *scc->ssbSubcarrierSpacing);
  }

  const uint32_t ssb_offset0 = *scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencySSB - scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA;

  return (ssb_offset0/(ratio*12) - 10); // absoluteFrequencySSB is the center of SSB

}


void schedule_ssb(frame_t frame, sub_frame_t slot,
                  NR_ServingCellConfigCommon_t *scc,
                  nfapi_nr_dl_tti_request_body_t *dl_req,
                  int i_ssb, uint8_t scoffset, uint16_t offset_pointa, uint32_t payload) {

  uint8_t beam_index = 0;
  nfapi_nr_dl_tti_request_pdu_t  *dl_config_pdu = &dl_req->dl_tti_pdu_list[dl_req->nPDUs];
  memset((void *) dl_config_pdu, 0,sizeof(nfapi_nr_dl_tti_request_pdu_t));
  dl_config_pdu->PDUType      = NFAPI_NR_DL_TTI_SSB_PDU_TYPE;
  dl_config_pdu->PDUSize      =2 + sizeof(nfapi_nr_dl_tti_ssb_pdu_rel15_t);

  AssertFatal(scc->physCellId!=NULL,"ServingCellConfigCommon->physCellId is null\n");
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.PhysCellId          = *scc->physCellId;
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.BetaPss             = 0;
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.SsbBlockIndex       = i_ssb;
  AssertFatal(scc->downlinkConfigCommon!=NULL,"scc->downlinkConfigCommonL is null\n");
  AssertFatal(scc->downlinkConfigCommon->frequencyInfoDL!=NULL,"scc->downlinkConfigCommon->frequencyInfoDL is null\n");
  AssertFatal(scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencySSB!=NULL,"scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencySSB is null\n");
  AssertFatal(scc->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.count==1,"Frequency Band list does not have 1 element (%d)\n",
              scc->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.count);
  AssertFatal(scc->ssbSubcarrierSpacing,"ssbSubcarrierSpacing is null\n");
  AssertFatal(scc->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.array[0],"band is null\n");

  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.SsbSubcarrierOffset = scoffset; //kSSB
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.ssbOffsetPointA     = offset_pointa;
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.bchPayloadFlag      = 1;
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.bchPayload          = payload;
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.precoding_and_beamforming.num_prgs=1;
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.precoding_and_beamforming.prg_size=275; //1 PRG of max size for analogue beamforming
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.precoding_and_beamforming.dig_bf_interfaces=1;
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.precoding_and_beamforming.prgs_list[0].pm_idx = 0;
  dl_config_pdu->ssb_pdu.ssb_pdu_rel15.precoding_and_beamforming.prgs_list[0].dig_bf_interface_list[0].beam_idx = beam_index;
  dl_req->nPDUs++;

  LOG_D(MAC,"Scheduling ssb %d at frame %d and slot %d\n",i_ssb,frame,slot);

}

void schedule_nr_mib(module_id_t module_idP, frame_t frameP, sub_frame_t slotP, uint8_t slots_per_frame, int nb_periods_per_frame){

  gNB_MAC_INST *gNB = RC.nrmac[module_idP];
  NR_COMMON_channels_t *cc;
  nfapi_nr_dl_tti_request_t      *dl_tti_request;
  nfapi_nr_dl_tti_request_body_t *dl_req;
  NR_MIB_t *mib = RC.nrrrc[module_idP]->carrier.mib.message.choice.mib;
  uint8_t num_tdd_period,num_ssb;
  int mib_sdu_length;
  int CC_id;

  for (CC_id = 0; CC_id < MAX_NUM_CCs; CC_id++) {
    cc = &gNB->common_channels[CC_id];
    NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
    dl_tti_request = &gNB->DL_req[CC_id];
    dl_req = &dl_tti_request->dl_tti_request_body;

    // get MIB every 8 frames
    if((slotP == 0) && (frameP & 7) == 0) {

      mib_sdu_length = mac_rrc_nr_data_req(module_idP, CC_id, frameP, MIBCH, 1, &cc->MIB_pdu.payload[0]);

      LOG_D(MAC,
            "[gNB %d] Frame %d : MIB->BCH  CC_id %d, Received %d bytes\n",
            module_idP,
            frameP,
            CC_id,
            mib_sdu_length);
    }

    int8_t ssb_period = *scc->ssb_periodicityServingCell;
    uint8_t ssb_frame_periodicity = 1;  // every how many frames SSB are generated

    if (ssb_period > 1) // 0 is every half frame
      ssb_frame_periodicity = 1 << (ssb_period -1);

    if (!(frameP%ssb_frame_periodicity) &&
        ((slotP<(slots_per_frame>>1)) || (ssb_period == 0))) {
      // schedule SSB only for given frames according to SSB periodicity
      // and in first half frame unless periodicity of 5ms
      int rel_slot;
      if (ssb_period == 0) // scheduling every half frame
        rel_slot = slotP%(slots_per_frame>>1);
      else
        rel_slot = slotP;

      NR_SubcarrierSpacing_t scs = *scc->ssbSubcarrierSpacing;
      const long band = *scc->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.array[0];
      uint16_t offset_pointa = get_ssboffset_pointa(scc,band);
      uint8_t ssbSubcarrierOffset = gNB->ssb_SubcarrierOffset;

      const BIT_STRING_t *shortBitmap = &scc->ssb_PositionsInBurst->choice.shortBitmap;
      const BIT_STRING_t *mediumBitmap = &scc->ssb_PositionsInBurst->choice.mediumBitmap;
      const BIT_STRING_t *longBitmap = &scc->ssb_PositionsInBurst->choice.longBitmap;

      uint16_t ssb_start_symbol;

      switch (scc->ssb_PositionsInBurst->present) {
        case 1:
          // short bitmap (<3GHz) max 4 SSBs
          for (int i_ssb=0; i_ssb<4; i_ssb++) {
            if ((shortBitmap->buf[0]>>(7-i_ssb))&0x01) {
              ssb_start_symbol = get_ssb_start_symbol(band,scs,i_ssb);
              // if start symbol is in current slot, schedule current SSB, fill VRB map and call get_type0_PDCCH_CSS_config_parameters
              if ((ssb_start_symbol/14) == rel_slot){
                schedule_ssb(frameP, slotP, scc, dl_req, i_ssb, ssbSubcarrierOffset, offset_pointa, (*(uint32_t*)cc->MIB_pdu.payload) & ((1<<24)-1));
                fill_ssb_vrb_map(cc, offset_pointa, ssb_start_symbol, CC_id);
                if (get_softmodem_params()->sa == 1) {
                  get_type0_PDCCH_CSS_config_parameters(&gNB->type0_PDCCH_CSS_config[i_ssb],
                                                        frameP,
                                                        mib,
                                                        slots_per_frame,
                                                        ssbSubcarrierOffset,
                                                        ssb_start_symbol,
                                                        scs,
                                                        FR1,
                                                        i_ssb,
                                                        offset_pointa);
                  gNB->type0_PDCCH_CSS_config[i_ssb].active = true;
                }
              }
            }
          }
          break;
        case 2:
          // medium bitmap (<6GHz) max 8 SSBs
          for (int i_ssb=0; i_ssb<8; i_ssb++) {
            if ((mediumBitmap->buf[0]>>(7-i_ssb))&0x01) {
              ssb_start_symbol = get_ssb_start_symbol(band,scs,i_ssb);
              // if start symbol is in current slot, schedule current SSB, fill VRB map and call get_type0_PDCCH_CSS_config_parameters
              if ((ssb_start_symbol/14) == rel_slot){
                schedule_ssb(frameP, slotP, scc, dl_req, i_ssb, ssbSubcarrierOffset, offset_pointa, (*(uint32_t*)cc->MIB_pdu.payload) & ((1<<24)-1));
                fill_ssb_vrb_map(cc, offset_pointa, ssb_start_symbol, CC_id);
                if (get_softmodem_params()->sa == 1) {
                  get_type0_PDCCH_CSS_config_parameters(&gNB->type0_PDCCH_CSS_config[i_ssb],
                                                        frameP,
                                                        mib,
                                                        slots_per_frame,
                                                        ssbSubcarrierOffset,
                                                        ssb_start_symbol,
                                                        scs,
                                                        FR1,
                                                        i_ssb,
                                                        offset_pointa);
                  gNB->type0_PDCCH_CSS_config[i_ssb].active = true;
                }
              }
            }
          }
          break;
        case 3:
          // long bitmap FR2 max 64 SSBs
          num_ssb = 0;
          for (int i_ssb=0; i_ssb<63; i_ssb++) {
            if ((longBitmap->buf[i_ssb/8]>>(7-i_ssb))&0x01) {
              ssb_start_symbol = get_ssb_start_symbol(band,scs,i_ssb);
              // if start symbol is in current slot, schedule current SSB, fill VRB map and call get_type0_PDCCH_CSS_config_parameters
              if ((ssb_start_symbol/14) == rel_slot){
                schedule_ssb(frameP, slotP, scc, dl_req, i_ssb, ssbSubcarrierOffset, offset_pointa, (*(uint32_t*)cc->MIB_pdu.payload) & ((1<<24)-1));
                fill_ssb_vrb_map(cc, offset_pointa, ssb_start_symbol, CC_id);
                num_tdd_period = rel_slot/(slots_per_frame/nb_periods_per_frame);
                gNB->tdd_beam_association[num_tdd_period]=i_ssb;
                num_ssb++;
                AssertFatal(num_ssb<2,"beamforming currently not supported for more than one SSB per slot\n");
                if (get_softmodem_params()->sa == 1) {
                  get_type0_PDCCH_CSS_config_parameters(&gNB->type0_PDCCH_CSS_config[i_ssb],
                                                        frameP,
                                                        mib,
                                                        slots_per_frame,
                                                        ssbSubcarrierOffset,
                                                        ssb_start_symbol,
                                                        scs,
                                                        FR2,
                                                        i_ssb,
                                                        offset_pointa);
                  gNB->type0_PDCCH_CSS_config[i_ssb].active = true;
                }
              }
            }
          }
          break;
        default:
          AssertFatal(0,"SSB bitmap size value %d undefined (allowed values 1,2,3)\n",
                      scc->ssb_PositionsInBurst->present);
      }
    }
  }
}


void schedule_nr_SI(module_id_t module_idP, frame_t frameP, sub_frame_t subframeP) {
//----------------------------------------  
}

void fill_ssb_vrb_map (NR_COMMON_channels_t *cc, int rbStart,  uint16_t symStart, int CC_id) {

  AssertFatal(*cc->ServingCellConfigCommon->ssbSubcarrierSpacing !=
              NR_SubcarrierSpacing_kHz240,
              "240kHZ subcarrier won't work with current VRB map because a single SSB might be across 2 slots\n");

  uint16_t *vrb_map = cc[CC_id].vrb_map;

  for (int rb = 0; rb < 20; rb++)
    vrb_map[rbStart + rb] = 15<<symStart;

}

void schedule_control_sib1(module_id_t module_id,
                           int CC_id,
                           NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config,
                           int time_domain_allocation,
                           uint8_t mcsTableIdx,
                           uint8_t mcs,
                           uint8_t candidate_idx,
                           int num_total_bytes) {

  gNB_MAC_INST *gNB_mac = RC.nrmac[module_id];
  NR_ServingCellConfigCommon_t *servingcellconfigcommon = gNB_mac->common_channels[CC_id].ServingCellConfigCommon;
  uint16_t *vrb_map = RC.nrmac[module_id]->common_channels[CC_id].vrb_map;

  if (gNB_mac->sched_ctrlCommon == NULL){
    gNB_mac->sched_ctrlCommon = calloc(1,sizeof(*gNB_mac->sched_ctrlCommon));
    gNB_mac->sched_ctrlCommon->search_space = calloc(1,sizeof(*gNB_mac->sched_ctrlCommon->search_space));
    gNB_mac->sched_ctrlCommon->coreset = calloc(1,sizeof(*gNB_mac->sched_ctrlCommon->coreset));
    gNB_mac->sched_ctrlCommon->active_bwp = calloc(1,sizeof(*gNB_mac->sched_ctrlCommon->active_bwp));
    fill_default_searchSpaceZero(gNB_mac->sched_ctrlCommon->search_space);
    fill_default_coresetZero(gNB_mac->sched_ctrlCommon->coreset,servingcellconfigcommon);
    fill_default_initialDownlinkBWP(gNB_mac->sched_ctrlCommon->active_bwp,servingcellconfigcommon);
  }
  gNB_mac->sched_ctrlCommon->active_bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->dmrs_AdditionalPosition = NULL;

  gNB_mac->sched_ctrlCommon->time_domain_allocation = time_domain_allocation;
  gNB_mac->sched_ctrlCommon->mcsTableIdx = mcsTableIdx;
  gNB_mac->sched_ctrlCommon->mcs = mcs;
  gNB_mac->sched_ctrlCommon->num_total_bytes = num_total_bytes;

  uint8_t nr_of_candidates;
  find_aggregation_candidates(&gNB_mac->sched_ctrlCommon->aggregation_level, &nr_of_candidates, gNB_mac->sched_ctrlCommon->search_space);

  gNB_mac->sched_ctrlCommon->cce_index = allocate_nr_CCEs(RC.nrmac[module_id],
                                                          gNB_mac->sched_ctrlCommon->active_bwp,
                                                          gNB_mac->sched_ctrlCommon->coreset,
                                                          gNB_mac->sched_ctrlCommon->aggregation_level,
                                                          0,
                                                          candidate_idx,
                                                          nr_of_candidates);

  AssertFatal(gNB_mac->sched_ctrlCommon->cce_index >= 0, "Could not find CCE for coreset0\n");

  const uint16_t bwpSize = type0_PDCCH_CSS_config->num_rbs;
  int rbStart = type0_PDCCH_CSS_config->cset_start_rb;

  // Calculate number of symbols
  struct NR_PDSCH_TimeDomainResourceAllocationList *tdaList = gNB_mac->sched_ctrlCommon->active_bwp->bwp_Common->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList;
  const int startSymbolAndLength = tdaList->list.array[gNB_mac->sched_ctrlCommon->time_domain_allocation]->startSymbolAndLength;
  int startSymbolIndex, nrOfSymbols;
  SLIV2SL(startSymbolAndLength, &startSymbolIndex, &nrOfSymbols);

  if (nrOfSymbols == 2) {
    gNB_mac->sched_ctrlCommon->numDmrsCdmGrpsNoData = 1;
  } else {
    gNB_mac->sched_ctrlCommon->numDmrsCdmGrpsNoData = 2;
  }

  // Calculate number of PRB_DMRS
  uint8_t N_PRB_DMRS = gNB_mac->sched_ctrlCommon->numDmrsCdmGrpsNoData * 6;
  uint16_t dlDmrsSymbPos = fill_dmrs_mask(gNB_mac->sched_ctrlCommon->active_bwp->bwp_Dedicated->pdsch_Config->choice.setup, gNB_mac->common_channels->ServingCellConfigCommon->dmrs_TypeA_Position, startSymbolIndex+nrOfSymbols);
  uint16_t dmrs_length = get_num_dmrs(dlDmrsSymbPos);

  int rbSize = 0;
  uint32_t TBS = 0;
  do {
    rbSize++;
    TBS = nr_compute_tbs(nr_get_Qm_dl(gNB_mac->sched_ctrlCommon->mcs, gNB_mac->sched_ctrlCommon->mcsTableIdx),
                         nr_get_code_rate_dl(gNB_mac->sched_ctrlCommon->mcs, gNB_mac->sched_ctrlCommon->mcsTableIdx),
                         rbSize, nrOfSymbols, N_PRB_DMRS * dmrs_length,0, 0,1) >> 3;
  } while (rbStart + rbSize < bwpSize && !vrb_map[rbStart + rbSize] && TBS < gNB_mac->sched_ctrlCommon->num_total_bytes);

  gNB_mac->sched_ctrlCommon->rbSize = rbSize;
  gNB_mac->sched_ctrlCommon->rbStart = 0;

  LOG_D(MAC,"SLIV = %i\n", startSymbolAndLength);
  LOG_D(MAC,"startSymbolIndex = %i\n", startSymbolIndex);
  LOG_D(MAC,"nrOfSymbols = %i\n", nrOfSymbols);
  LOG_D(MAC,"rbSize = %i\n", gNB_mac->sched_ctrlCommon->rbSize);
  LOG_D(MAC,"TBS = %i\n", TBS);

  // Mark the corresponding RBs as used
  for (int rb = 0; rb < gNB_mac->sched_ctrlCommon->rbSize; rb++) {
    vrb_map[rb + rbStart] = 1;
  }
}

void nr_fill_nfapi_dl_sib1_pdu(int Mod_idP,
                               nfapi_nr_dl_tti_request_body_t *dl_req,
                               NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config,
                               uint32_t TBS,
                               int StartSymbolIndex,
                               int NrOfSymbols) {

  gNB_MAC_INST *gNB_mac = RC.nrmac[Mod_idP];
  NR_COMMON_channels_t *cc = gNB_mac->common_channels;
  NR_ServingCellConfigCommon_t *scc = cc->ServingCellConfigCommon;
  NR_CellGroupConfig_t *secondaryCellGroup = gNB_mac->secondaryCellGroupCommon;
  NR_BWP_Downlink_t *bwp = gNB_mac->sched_ctrlCommon->active_bwp;

  nfapi_nr_dl_tti_request_pdu_t *dl_tti_pdcch_pdu = &dl_req->dl_tti_pdu_list[dl_req->nPDUs];
  memset((void*)dl_tti_pdcch_pdu,0,sizeof(nfapi_nr_dl_tti_request_pdu_t));
  dl_tti_pdcch_pdu->PDUType = NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE;
  dl_tti_pdcch_pdu->PDUSize = (uint8_t)(2+sizeof(nfapi_nr_dl_tti_pdcch_pdu));
  dl_req->nPDUs += 1;
  nfapi_nr_dl_tti_pdcch_pdu_rel15_t *pdcch_pdu_rel15 = &dl_tti_pdcch_pdu->pdcch_pdu.pdcch_pdu_rel15;
  nr_configure_pdcch(pdcch_pdu_rel15,
                     gNB_mac->sched_ctrlCommon->search_space,
                     gNB_mac->sched_ctrlCommon->coreset,
                     scc,
                     bwp);

  // TODO: This assignment should be done in function nr_configure_pdcch()
  pdcch_pdu_rel15->BWPSize = gNB_mac->type0_PDCCH_CSS_config->num_rbs;
  pdcch_pdu_rel15->BWPStart = gNB_mac->type0_PDCCH_CSS_config->cset_start_rb;

  nfapi_nr_dl_tti_request_pdu_t *dl_tti_pdsch_pdu = &dl_req->dl_tti_pdu_list[dl_req->nPDUs];
  memset((void*)dl_tti_pdsch_pdu,0,sizeof(nfapi_nr_dl_tti_request_pdu_t));
  dl_tti_pdsch_pdu->PDUType = NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE;
  dl_tti_pdsch_pdu->PDUSize = (uint8_t)(2+sizeof(nfapi_nr_dl_tti_pdsch_pdu));
  dl_req->nPDUs += 1;
  nfapi_nr_dl_tti_pdsch_pdu_rel15_t *pdsch_pdu_rel15 = &dl_tti_pdsch_pdu->pdsch_pdu.pdsch_pdu_rel15;

  pdcch_pdu_rel15->CoreSetType = NFAPI_NR_CSET_CONFIG_MIB_SIB1;

  pdsch_pdu_rel15->pduBitmap = 0;
  pdsch_pdu_rel15->rnti = SI_RNTI;
  pdsch_pdu_rel15->pduIndex = gNB_mac->pdu_index[0]++;

  pdsch_pdu_rel15->BWPSize  = type0_PDCCH_CSS_config->num_rbs;
  pdsch_pdu_rel15->BWPStart = type0_PDCCH_CSS_config->cset_start_rb;

  pdsch_pdu_rel15->SubcarrierSpacing = bwp->bwp_Common->genericParameters.subcarrierSpacing;
  if (bwp->bwp_Common->genericParameters.cyclicPrefix) {
    pdsch_pdu_rel15->CyclicPrefix = *bwp->bwp_Common->genericParameters.cyclicPrefix;
  } else {
    pdsch_pdu_rel15->CyclicPrefix = 0;
  }

  pdsch_pdu_rel15->NrOfCodewords = 1;
  pdsch_pdu_rel15->targetCodeRate[0] = nr_get_code_rate_dl(gNB_mac->sched_ctrlCommon->mcs,0);
  pdsch_pdu_rel15->qamModOrder[0] = 2;
  pdsch_pdu_rel15->mcsIndex[0] = gNB_mac->sched_ctrlCommon->mcs;
  pdsch_pdu_rel15->mcsTable[0] = 0;
  pdsch_pdu_rel15->rvIndex[0] = nr_rv_round_map[0];
  pdsch_pdu_rel15->dataScramblingId = *scc->physCellId;
  pdsch_pdu_rel15->nrOfLayers = 1;
  pdsch_pdu_rel15->transmissionScheme = 0;
  pdsch_pdu_rel15->refPoint = 1;
  pdsch_pdu_rel15->dmrsConfigType = gNB_mac->sched_ctrlCommon->active_bwp->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->dmrs_Type == NULL ? 0 : 1;
  pdsch_pdu_rel15->dlDmrsScramblingId = *scc->physCellId;
  pdsch_pdu_rel15->SCID = 0;
  pdsch_pdu_rel15->numDmrsCdmGrpsNoData = gNB_mac->sched_ctrlCommon->numDmrsCdmGrpsNoData;
  pdsch_pdu_rel15->dmrsPorts = 1;
  pdsch_pdu_rel15->resourceAlloc = 1;
  pdsch_pdu_rel15->rbStart = gNB_mac->sched_ctrlCommon->rbStart;
  pdsch_pdu_rel15->rbSize = gNB_mac->sched_ctrlCommon->rbSize;
  pdsch_pdu_rel15->VRBtoPRBMapping = 0;
  pdsch_pdu_rel15->qamModOrder[0] = nr_get_Qm_dl(gNB_mac->sched_ctrlCommon->mcs, gNB_mac->sched_ctrlCommon->mcsTableIdx);
  pdsch_pdu_rel15->TBSize[0] = TBS;
  pdsch_pdu_rel15->mcsTable[0] = gNB_mac->sched_ctrlCommon->mcsTableIdx;
  pdsch_pdu_rel15->StartSymbolIndex = StartSymbolIndex;
  pdsch_pdu_rel15->NrOfSymbols = NrOfSymbols;

  pdsch_pdu_rel15->dlDmrsSymbPos = fill_dmrs_mask(bwp->bwp_Dedicated->pdsch_Config->choice.setup, scc->dmrs_TypeA_Position, pdsch_pdu_rel15->StartSymbolIndex+pdsch_pdu_rel15->NrOfSymbols);

  LOG_D(MAC,"dlDmrsSymbPos = 0x%x\n", pdsch_pdu_rel15->dlDmrsSymbPos);

  /* Fill PDCCH DL DCI PDU */
  nfapi_nr_dl_dci_pdu_t *dci_pdu = &pdcch_pdu_rel15->dci_pdu[pdcch_pdu_rel15->numDlDci];
  pdcch_pdu_rel15->numDlDci++;
  dci_pdu->RNTI = SI_RNTI;
  dci_pdu->ScramblingId = *scc->physCellId;
  dci_pdu->ScramblingRNTI = 0;
  dci_pdu->AggregationLevel = gNB_mac->sched_ctrlCommon->aggregation_level;
  dci_pdu->CceIndex = gNB_mac->sched_ctrlCommon->cce_index;
  dci_pdu->beta_PDCCH_1_0 = 0;
  dci_pdu->powerControlOffsetSS = 1;

  /* DCI payload */
  dci_pdu_rel15_t dci_payload;
  memset(&dci_payload, 0, sizeof(dci_pdu_rel15_t));

  dci_payload.bwp_indicator.val = gNB_mac->sched_ctrlCommon->active_bwp->bwp_Id;

  // frequency domain assignment
  dci_payload.frequency_domain_assignment.val = PRBalloc_to_locationandbandwidth0(
      pdsch_pdu_rel15->rbSize, pdsch_pdu_rel15->rbStart, type0_PDCCH_CSS_config->num_rbs);

  dci_payload.time_domain_assignment.val = gNB_mac->sched_ctrlCommon->time_domain_allocation;
  dci_payload.mcs = gNB_mac->sched_ctrlCommon->mcs;
  dci_payload.rv = pdsch_pdu_rel15->rvIndex[0];
  dci_payload.harq_pid = 0;
  dci_payload.ndi = 0;
  dci_payload.dai[0].val = 0;
  dci_payload.tpc = 0; // table 7.2.1-1 in 38.213
  dci_payload.pucch_resource_indicator = 0;
  dci_payload.pdsch_to_harq_feedback_timing_indicator.val = 0;
  dci_payload.antenna_ports.val = 0;
  dci_payload.dmrs_sequence_initialization.val = pdsch_pdu_rel15->SCID;

  int dci_format = NR_DL_DCI_FORMAT_1_0;
  int rnti_type = NR_RNTI_SI;

  fill_dci_pdu_rel15(scc,
                     secondaryCellGroup,
                     &pdcch_pdu_rel15->dci_pdu[pdcch_pdu_rel15->numDlDci - 1],
                     &dci_payload,
                     dci_format,
                     rnti_type,
                     pdsch_pdu_rel15->BWPSize,
                     gNB_mac->sched_ctrlCommon->active_bwp->bwp_Id);

  LOG_D(MAC,"BWPSize: %i\n", pdcch_pdu_rel15->BWPSize);
  LOG_D(MAC,"BWPStart: %i\n", pdcch_pdu_rel15->BWPStart);
  LOG_D(MAC,"SubcarrierSpacing: %i\n", pdcch_pdu_rel15->SubcarrierSpacing);
  LOG_D(MAC,"CyclicPrefix: %i\n", pdcch_pdu_rel15->CyclicPrefix);
  LOG_D(MAC,"StartSymbolIndex: %i\n", pdcch_pdu_rel15->StartSymbolIndex);
  LOG_D(MAC,"DurationSymbols: %i\n", pdcch_pdu_rel15->DurationSymbols);
  for(int n=0;n<6;n++) LOG_D(MAC,"FreqDomainResource[%i]: %x\n",n, pdcch_pdu_rel15->FreqDomainResource[n]);
  LOG_D(MAC,"CceRegMappingType: %i\n", pdcch_pdu_rel15->CceRegMappingType);
  LOG_D(MAC,"RegBundleSize: %i\n", pdcch_pdu_rel15->RegBundleSize);
  LOG_D(MAC,"InterleaverSize: %i\n", pdcch_pdu_rel15->InterleaverSize);
  LOG_D(MAC,"CoreSetType: %i\n", pdcch_pdu_rel15->CoreSetType);
  LOG_D(MAC,"ShiftIndex: %i\n", pdcch_pdu_rel15->ShiftIndex);
  LOG_D(MAC,"precoderGranularity: %i\n", pdcch_pdu_rel15->precoderGranularity);
  LOG_D(MAC,"numDlDci: %i\n", pdcch_pdu_rel15->numDlDci);

}

void schedule_nr_sib1(module_id_t module_idP, frame_t frameP, sub_frame_t slotP) {

  LOG_D(MAC,"Schedule_nr_sib1: frameP = %i, slotP = %i\n", frameP, slotP);

  // TODO: Get these values from RRC
  const int CC_id = 0;
  int time_domain_allocation = 0;
  uint8_t mcsTableIdx = 0;
  uint8_t mcs = 6;
  uint8_t candidate_idx = 0;

  gNB_MAC_INST *gNB_mac = RC.nrmac[module_idP];
  NR_ServingCellConfigCommon_t *scc = gNB_mac->common_channels[CC_id].ServingCellConfigCommon;

  int L_max;
  switch (scc->ssb_PositionsInBurst->present) {
    case 1:
      L_max = 4;
      break;
    case 2:
      L_max = 8;
      break;
    case 3:
      L_max = 64;
      break;
    default:
      AssertFatal(0,"SSB bitmap size value %d undefined (allowed values 1,2,3)\n",
                  scc->ssb_PositionsInBurst->present);
  }

  for (int i=0; i<L_max; i++) {

    NR_Type0_PDCCH_CSS_config_t *type0_PDCCH_CSS_config = &gNB_mac->type0_PDCCH_CSS_config[i];

    if((frameP%2 == type0_PDCCH_CSS_config->sfn_c) &&
       (slotP == type0_PDCCH_CSS_config->n_0) &&
       (type0_PDCCH_CSS_config->num_rbs > 0) &&
       (type0_PDCCH_CSS_config->active == true)) {

      LOG_D(MAC,"> SIB1 transmission\n");

      // Get SIB1
      uint8_t sib1_payload[NR_MAX_SIB_LENGTH/8];
      uint8_t sib1_sdu_length = mac_rrc_nr_data_req(module_idP, CC_id, frameP, BCCH, 1, sib1_payload);
      LOG_D(MAC,"sib1_sdu_length = %i\n", sib1_sdu_length);
      LOG_D(MAC,"SIB1: \n");
      for (int i=0;i<sib1_sdu_length;i++) LOG_D(MAC,"byte %d : %x\n",i,((uint8_t*)sib1_payload)[i]);

      // Configure sched_ctrlCommon for SIB1
      schedule_control_sib1(module_idP, CC_id, type0_PDCCH_CSS_config, time_domain_allocation, mcsTableIdx, mcs, candidate_idx, sib1_sdu_length);

      // Calculate number of symbols
      int startSymbolIndex, nrOfSymbols;
      struct NR_PDSCH_TimeDomainResourceAllocationList *tdaList = gNB_mac->sched_ctrlCommon->active_bwp->bwp_Common->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList;
      const int startSymbolAndLength = tdaList->list.array[gNB_mac->sched_ctrlCommon->time_domain_allocation]->startSymbolAndLength;
      SLIV2SL(startSymbolAndLength, &startSymbolIndex, &nrOfSymbols);

      // Calculate number of PRB_DMRS
      uint8_t N_PRB_DMRS = gNB_mac->sched_ctrlCommon->numDmrsCdmGrpsNoData * 6;
      uint16_t dlDmrsSymbPos = fill_dmrs_mask(gNB_mac->sched_ctrlCommon->active_bwp->bwp_Dedicated->pdsch_Config->choice.setup,
                                              gNB_mac->common_channels->ServingCellConfigCommon->dmrs_TypeA_Position, startSymbolIndex+nrOfSymbols);
      uint16_t dmrs_length = get_num_dmrs(dlDmrsSymbPos);

      const uint32_t TBS = nr_compute_tbs(nr_get_Qm_dl(gNB_mac->sched_ctrlCommon->mcs, gNB_mac->sched_ctrlCommon->mcsTableIdx),
                                          nr_get_code_rate_dl(gNB_mac->sched_ctrlCommon->mcs, gNB_mac->sched_ctrlCommon->mcsTableIdx),
                                          gNB_mac->sched_ctrlCommon->rbSize, nrOfSymbols, N_PRB_DMRS * dmrs_length,0 ,0 ,1 ) >> 3;

      nfapi_nr_dl_tti_request_body_t *dl_req = &gNB_mac->DL_req[CC_id].dl_tti_request_body;
      nr_fill_nfapi_dl_sib1_pdu(module_idP, dl_req, type0_PDCCH_CSS_config, TBS, startSymbolIndex, nrOfSymbols);

      const int ntx_req = gNB_mac->TX_req[CC_id].Number_of_PDUs;
      nfapi_nr_pdu_t *tx_req = &gNB_mac->TX_req[CC_id].pdu_list[ntx_req];

      // Data to be transmitted
      bzero(tx_req->TLVs[0].value.direct,MAX_NR_DLSCH_PAYLOAD_BYTES);
      memcpy(tx_req->TLVs[0].value.direct, sib1_payload, sib1_sdu_length);

      tx_req->PDU_length = TBS;
      tx_req->PDU_index  = gNB_mac->pdu_index[0]++;
      tx_req->num_TLV = 1;
      tx_req->TLVs[0].length = TBS + 2;
      gNB_mac->TX_req[CC_id].Number_of_PDUs++;
      gNB_mac->TX_req[CC_id].SFN = frameP;
      gNB_mac->TX_req[CC_id].Slot = slotP;

      type0_PDCCH_CSS_config->active = false;
    }
  }
}

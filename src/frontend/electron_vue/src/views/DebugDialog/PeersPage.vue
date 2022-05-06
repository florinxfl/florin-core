<template>
  <div class="peers-page">
    <app-section>
      <h4>Peers</h4>
      <div class="peer-header-item">
        <h4 style="width: 30%">Node Id</h4>
        <h4 style="width: 30%; flex: 1">Node/Service</h4>
        <h4 style="width: 30%">User Agent</h4>
      </div>
      <div class="peers-list" v-for="(peer, index) in peers" :key="peer.id">
        <div class="peer-item" @click="showPeerDetails(peer, index)">
          <div class="peer-item-cell">{{ peer.id }}</div>
          <div class="peer-item-cell" style="flex: 1">{{ peer.ip }}</div>
          <div class="peer-item-cell">{{ peer.userAgent }}</div>
        </div>
      </div>
    </app-section>
    <app-section>
      <h4>Banned Peers</h4>
    </app-section>
  </div>
</template>

<script>
import { P2pNetworkController } from "../../unity/Controllers";
import PeerDetailsDialog from "../../components/PeerDetailsDialog";
import EventBus from "../../EventBus";

export default {
  data() {
    return {
      peers: null,
      bannedPeers: null
    };
  },
  name: "PeersPage",
  computed: {},
  methods: {
    getPeers() {
      const peers = P2pNetworkController.GetPeerInfo();
      const bannedPeers = P2pNetworkController.listBannedPeers();
      this.peers = peers;
      this.bannedPeers = bannedPeers;
    },
    showPeerDetails(peer) {
      EventBus.$emit("show-dialog", {
        title: "Sample Title",
        component: PeerDetailsDialog,
        componentProps: {
          peer: peer
        },
        showButtons: false
      });
    }
  },
  mounted() {
    this.getPeers();
  }
};
</script>

<style lang="less" scoped>
.peers-page {
  width: 100%;
  height: 100%;
}

.peers-list:not(:first-child) > h4 {
  margin-top: 30px;
}

.peer-header-item {
  display: flex;
  flex-direction: row;
  width: 100%;
}

.peer-item {
  display: flex;
  flex-direction: row;
  width: 100%;
  cursor: pointer;
}

.peer-item:hover {
  color: var(--primary-color);
  background: var(--hover-color);
}

.peer-item-cell {
  width: 30%;
  padding: 5px 10px 5px 0px;
  text-transform: uppercase;
}
</style>

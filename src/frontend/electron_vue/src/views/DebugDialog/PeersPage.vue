<template>
  <div class="peers-page">
    <h4>{{ $t("peers.peers") }}</h4>
    <div class="peer-header-item">
      <h4 style="width: 30%">{{ $t("peers.node_id") }}</h4>
      <h4 style="width: 30%; flex: 1">{{ $t("peers.node_service") }}</h4>
      <h4 style="width: 30%">{{ $t("peers.user_agent") }}</h4>
    </div>
    <app-section class="peer-section">
      <div class="peers-list" v-for="peer in peers" :key="peer.id">
        <div class="peer-item" @click="showPeerDetails(peer, false)" @contextmenu.prevent="onRightClick">
          <div class="peer-item-cell">{{ peer.id }}</div>
          <div class="peer-item-cell" style="flex: 1">{{ peer.ip }}</div>
          <div class="peer-item-cell">{{ peer.userAgent }}</div>
        </div>
      </div>
    </app-section>
    <div class="subheader-row">
      <h4 style="margin-bottom: 0px; flex: 1">{{ $t("peers.banned_peers") }}</h4>
      <button @click="clearBannedPeers" v-if="bannedPeers && bannedPeers.length > 0" outlined class="small">
        Clear Banned
      </button>
    </div>
    <div class="peer-header-item">
      <h4 style="width: 30%">{{ $t("peers.address") }}</h4>
      <h4 style="width: 30%">{{ $t("peers.reason") }}</h4>
    </div>
    <app-section class="peer-section">
      <div class="peers-list" v-for="peer in bannedPeers" :key="peer.id">
        <div class="peer-item" @click="showPeerDetails(peer, true)">
          <div class="peer-item-cell">{{ peer.address }}</div>
          <div class="peer-item-cell" style="flex: 1">{{ peer.reason }}</div>
        </div>
      </div>
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
    onRightClick(e) {
      console.log(e.x);
      console.log(e.y);
      console.log(e.target);
      console.log("right clicked");
    },
    getPeers() {
      const peers = P2pNetworkController.GetPeerInfo();
      const bannedPeers = P2pNetworkController.ListBannedPeers();
      this.peers = peers;
      this.bannedPeers = bannedPeers;
    },
    showPeerDetails(peer, banned) {
      EventBus.$emit("show-dialog", {
        title: "Peer Details",
        component: PeerDetailsDialog,
        componentProps: {
          peer: peer,
          banned: banned
        },
        showButtons: false
      });
    },
    clearBannedPeers() {
      console.log("Clearing");
      P2pNetworkController.ClearBannedAsync().then(() => {
        this.getPeers();
      });
    }
  },
  mounted() {
    this.getPeers();
    EventBus.$on("close-dialog", this.getPeers);
  }
};
</script>

<style lang="less" scoped>
.peers-page {
  width: 100%;
  height: 100%;
}

.peer-section {
  height: 160px;
  overflow: scroll;
  border: solid 0.5px #888888;
  padding: 2px;
}

.peer-section::-webkit-scrollbar {
  display: none;
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

.subheader-row {
  display: flex;
  flex-direction: row;
  align-items: center;
  height: 40px;
}

button.small {
  height: 20px;
  line-height: 20px;
  font-size: 10px;
  margin-left: 5px;
  min-width: 150px;
}
</style>

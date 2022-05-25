<template>
  <div class="peer-details-dialog">
    <activity-indicator v-if="loading" :paddingHidden="true" />
    <div class="peer-info-row">
      <h5>Node ID</h5>
      <div>{{ peer.id }}</div>
    </div>
    <div class="peer-info-row">
      <h5>Node / Service</h5>
      <div>{{ peer.ip }}</div>
    </div>
    <div class="peer-info-row">
      <h5>Whitelisted</h5>
      <div>
        {{ !peer.whitelisted ? "No" : "Yes" }}
      </div>
    </div>
    <div class="peer-info-row">
      <h5>Direction</h5>
      <div>
        {{ peer.inbound ? "Inbound" : "Outbound" }}
      </div>
    </div>
    <div class="peer-info-row">
      <h5>User Agent / Version</h5>
      <div>{{ peer.userAgent }}</div>
    </div>
    <div class="peer-info-row">
      <h5>Services</h5>
      <div>
        {{ peer.services }}
      </div>
    </div>
    <div class="peer-info-row">
      <h5>Starting Block</h5>
      <div>
        {{ peer.start_height }}
      </div>
    </div>
    <div class="peer-info-row">
      <h5>Synced Headers</h5>
      <div>
        {{ peer.synced_height }}
      </div>
    </div>
    <div class="peer-info-row">
      <h5>Synced Blocks</h5>
      <div>
        {{ peer.common_height }}
      </div>
    </div>
    <div class="peer-info-row">
      <h5>Ban Score</h5>
      <div>
        {{ peer.banscore }}
      </div>
    </div>
    <div class="peer-info-row">
      <h5>Connection Time</h5>
      <div>{{ formatDate(peer.time_connected) }} s</div>
    </div>
    <div class="peer-info-row">
      <h5>Last Send</h5>
      <div>{{ formatDate(peer.last_send) }} s</div>
    </div>
    <div class="peer-info-row">
      <h5>Last Receive</h5>
      <div>{{ formatDate(peer.last_receive) }} s</div>
    </div>
    <div class="peer-info-row">
      <h5>Sent</h5>
      <div>{{ peer.send_bytes }} B</div>
    </div>
    <div class="peer-info-row">
      <h5>Received</h5>
      <div>{{ peer.receive_bytes }} B</div>
    </div>
    <div class="peer-info-row">
      <h5>Ping Time</h5>
      <div>{{ peer.latency }} B</div>
    </div>
    <div class="peer-info-row">
      <h5>Time Offset</h5>
      <div>{{ peer.time_offset }}</div>
    </div>
    <div v-if="!banned" style="margin-top: 20px" class="button-row">
      <button @click="disconnectPeer(peer)" outlined class="small">
        {{ $t("buttons.disconnect") }}
      </button>
      <button @click="banPeer(3600)" outlined class="small">
        {{ $t("buttons.ban_hour") }}
      </button>
      <button @click="banPeer(86400)" outlined class="small">
        {{ $t("buttons.ban_day") }}
      </button>
      <button @click="banPeer(604800)" outlined class="small">
        {{ $t("buttons.ban_week") }}
      </button>
      <button @click="banPeer(31449600)" outlined class="small">
        {{ $t("buttons.ban_year") }}
      </button>
    </div>
    <div v-else style="margin-top: 20px" class="button-row">
      <button @click="unBanPeer(peer)" outlined class="small">
        {{ $t("buttons.un_ban") }}
      </button>
    </div>
  </div>
</template>

<script>
import EventBus from "../EventBus";
import { P2pNetworkController } from "../unity/Controllers";
import ActivityIndicator from "./../components/ActivityIndicator.vue";

export default {
  data() {
    return {
      loading: false
    };
  },
  name: "PeerDetailsDialog",
  props: {
    peer: {
      type: Object,
      default: null
    },
    banned: {
      type: Boolean,
      default: false
    }
  },
  components: {
    ActivityIndicator
  },
  methods: {
    close() {
      EventBus.$emit("close-dialog");
    },
    formatDate(date) {
      const now = Date.now() / 1000;
      return parseInt(now - date);
    },
    disconnectPeer() {
      this.loading = true;
      P2pNetworkController.DisconnectPeerAsync(this.peer.id).then(() => {
        setTimeout(() => {
          this.loading = false;
          EventBus.$emit("close-dialog");
        }, 1000);
      });
    },
    banPeer(interval) {
      this.loading = true;

      P2pNetworkController.BanPeerAsync(this.peer.addrBind, interval)
        .then(() => {
          setTimeout(() => {
            this.loading = false;
            EventBus.$emit("close-dialog");
          }, 1000);
        })
        .catch(err => {
          console.log(err.message);
        });
    },
    unBanPeer() {
      this.loading = true;
      P2pNetworkController.UnbanPeerAsync(this.peer.address)
        .then(() => {
          setTimeout(() => {
            this.loading = false;
            EventBus.$emit("close-dialog");
          }, 1000);
        })
        .catch(err => {
          console.log(err.message);
        });
    }
  }
};
</script>

<style lang="less" scoped>
.peer-details-dialog {
  text-align: left;
  height: 350px;
}
h5 {
  flex: 1;
}

.button-row {
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
}

button.small {
  height: 20px;
  line-height: 20px;
  font-size: 10px;
  padding: 0 10px;
  margin-left: 5px;
  min-width: 150px;
  margin-bottom: 10px;
}

.peer-info-row {
  padding-bottom: 14px;
  display: flex;
  flex-direction: row;
  width: 400px;
}
</style>

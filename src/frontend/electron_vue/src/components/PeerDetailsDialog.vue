<template>
  <div class="peer-details-dialog">
    <activity-indicator v-if="loading" :paddingHidden="true" />
    <div v-if="!banned">
      <div class="peer-info-row">
        <h5>{{ $t("peers.node_id") }}</h5>
        <div>{{ peer.id }}</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.node_service") }}</h5>
        <div>{{ peer.ip }}</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.whitelisted") }}</h5>
        <div>
          {{ !peer.whitelisted ? "No" : "Yes" }}
        </div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.direction") }}</h5>
        <div>
          {{ peer.inbound ? "Inbound" : "Outbound" }}
        </div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.user_agent") }}</h5>
        <div>{{ peer.userAgent }}</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.services") }}</h5>
        <div>
          {{ peer.services }}
        </div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.starting_block") }}</h5>
        <div>
          {{ peer.start_height }}
        </div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.synced_headers") }}</h5>
        <div>
          {{ peer.synced_height }}
        </div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.synced_blocks") }}</h5>
        <div>
          {{ peer.common_height }}
        </div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.ban_score") }}</h5>
        <div>
          {{ peer.banscore }}
        </div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.connection_time") }}</h5>
        <div>{{ formatDateFrom(peer.time_connected) }} s</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.last_send") }}</h5>
        <div>{{ formatDateFrom(peer.last_send) }} s</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.last_receive") }}</h5>
        <div>{{ formatDateFrom(peer.last_receive) }} s</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.sent") }}</h5>
        <div>{{ peer.send_bytes }} B</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.received") }}</h5>
        <div>{{ peer.receive_bytes }} B</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.ping_time") }}</h5>
        <div>{{ peer.latency }} ms</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.time_offset") }}</h5>
        <div>{{ peer.time_offset }}</div>
      </div>
    </div>
    <div v-else>
      <div class="peer-info-row">
        <h5>{{ $t("peers.address") }}</h5>
        <div>{{ peer.address }}</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.banned_from") }}</h5>
        <div>{{ formatDate(peer.banned_from) }}</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.banned_until") }}</h5>
        <div>{{ formatDate(peer.banned_until) }}</div>
      </div>
      <div class="peer-info-row">
        <h5>{{ $t("peers.reason") }}</h5>
        <div style="text-transform: capitalize">{{ peer.reason }}</div>
      </div>
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
    formatDateFrom(date) {
      const now = Date.now() / 1000;
      return parseInt(now - date);
    },
    formatDate(date) {
      const dateTimeString = new Date(date * 1000).toLocaleString();
      return dateTimeString;
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

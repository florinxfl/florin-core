<template>
  <div class="peer-details-dialog">
    <div class="main">
      <h5>Peer Details</h5>
      <div class="tx-hash">{{ peer.id }}</div>
    </div>
  </div>
</template>

<script>
import EventBus from "../EventBus";

export default {
  name: "PeerDetailsDialog",
  props: {
    peer: {
      type: Object,
      default: null
    }
  },
  methods: {
    close() {
      EventBus.$emit("close-dialog");
    }
  }
};
</script>

<style lang="less" scoped>
.peer-details-dialog {
  text-align: center;

  & > h4 {
    margin: 25px 0 0 0;
  }
}
</style>

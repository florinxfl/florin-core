<template>
  <div class="link-witness-view flex-col">
    <portal to="header-slot">
      <main-header :title="$t('link_holding_account.title')"></main-header>
    </portal>

    <div class="main">
      <h4>{{ $t("link_holding_account.title") }}</h4>
      <p class="information">{{ $t("link_holding_account.information") }}</p>
      <div class="qr" @click="copyQr">
        <vue-qrcode
          ref="qrcode"
          class="qrcode"
          :width="280"
          :margin="0"
          :value="witnessKey"
          :color="{ dark: '#000000', light: '#ffffff' }"
        />
      </div>
    </div>
    <div class="address-row flex-row">
      <div class="flex-1" />
      <clipboard-field
        class="address"
        :value="witnessKey"
        confirmation="receive_coins.address_copied_to_clipboard"
      ></clipboard-field>
      <div class="flex-1" />
    </div>
  </div>
</template>

<script>
import { mapState, mapGetters } from "vuex";
import { clipboard, nativeImage } from "electron";
import { AccountsController } from "../../../unity/Controllers";
import VueQrcode from "vue-qrcode";

export default {
  name: "LinkHoldingAccount",
  components: {
    VueQrcode
  },
  data() {
    return {
      witnessKey: "sample-witness-key"
    };
  },
  computed: {
    ...mapState("wallet", ["walletPassword"]),
    ...mapGetters("wallet", ["account"])
  },
  mounted() {
    // LUKE TODO: Fix this when the function is working
    AccountsController.GetWitnessKeyURI(this.account.UUID);
  },
  methods: {
    copyQr() {
      let img = nativeImage.createFromDataURL(this.$refs.qrcode.$el.src);
      clipboard.writeImage(img);
    }
  }
};
</script>

<style lang="less" scoped>
.link-witness-view {
  height: 100%;
  text-align: center;
  & .information {
    margin: 0 0 30px 0;
  }
  & .qr {
    text-align: center;
    cursor: pointer;
    margin: 0 auto;
  }
  & .qrcode {
    width: 100%;
    max-width: 140px;
  }
  & .address-row {
    width: 100%;
    text-align: center;
  }
  & .address {
    margin: 5px 0 0 0;
    font-weight: 500;
    font-size: 1em;
    line-height: 1.4em;
  }
}
</style>

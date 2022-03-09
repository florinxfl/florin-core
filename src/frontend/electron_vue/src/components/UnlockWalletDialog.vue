<template>
  <div class="modal-mask flex-col" v-if="visible" @click="onCancel">
    <div class="modal-container" @click.stop>
      <div class="header">
        <span>{{ $t(this.options.title) }}</span>
        <div class="close" @click="onCancel">
          <span class="icon">
            <fa-icon :icon="['fal', 'times']" />
          </span>
        </div>
      </div>
      <div class="content">
        <app-form-field title="Unlock wallet for..." v-if="options.timeout == null">
          <select-list :options="timeoutOptions" :default="timeoutOptions[0]" v-model="timeout" />
        </app-form-field>
        <content-wrapper :content="options.message">
          <app-form-field title="common.password">
            <input ref="pwd" v-model="password" type="password" @keydown.enter="onOk" />
          </app-form-field>
        </content-wrapper>
      </div>
      <app-button-section class="buttons">
        <template v-slot:right>
          <button @click="onUnlock">{{ $t("buttons.unlock") }}</button>
        </template>
      </app-button-section>
    </div>
  </div>
</template>

<script>
import { mapState } from "vuex";
import { LibraryController } from "../unity/Controllers";
import EventBus from "../EventBus";

let theTimeout;

export default {
  name: "UnlockWalletDialog",
  data() {
    return {
      visible: false,
      isUnlocked: null,
      options: {},
      password: ""
    };
  },
  mounted() {
    EventBus.$on("unlock-wallet", this.unlockWallet);
    EventBus.$on("lock-wallet", this.lockWallet);
  },
  beforeDestroy() {
    EventBus.$off("unlock-wallet", this.unlockWallet);
    EventBus.$off("lock-wallet", this.lockWallet);
  },
  computed: {
    ...mapState("wallet", ["unlocked"]),
    timeoutOptions() {
      return [
        { value: 60, label: "1 minute" },
        { value: 300, label: "5 minutes" },
        { value: 6000, label: "10 minutes" }
      ];
    }
  },
  methods: {
    async lockWallet() {
      clearTimeout(theTimeout);
      await LibraryController.LockWalletAsync();
      this.$store.dispatch("wallet/SET_WALLET_UNLOCKED", false);
    },
    async unlockWallet(options) {
      // note:
      // it can happen if unlocked is true now, but is false right after the callback
      // find a solution for this situaton...
      if (this.unlocked) {
        if (typeof options.callback === "function") {
          return options.callback(true);
        }
      }

      this.options = {
        title: "Unlock wallet",
        message: null,
        timeout: null,
        ...options
      };

      this.visible = true;
      this.$nextTick(() => {
        this.$refs.pwd.focus();
      });
    },
    onCancel() {
      if (this.options.callback) this.options.callback(false);
      this.password = null;
      this.visible = false;
    },
    async onUnlock() {
      var result = await LibraryController.UnlockWalletAsync(this.password);
      if (result) {
        this.$store.dispatch("wallet/SET_WALLET_UNLOCKED", true);

        let timeout = this.options.timeout || this.timeout.value;

        theTimeout = setTimeout(async () => {
          await LibraryController.LockWalletAsync();
          this.$store.dispatch("wallet/SET_WALLET_UNLOCKED", false);
        }, 1000 * timeout);

        if (this.options.callback) this.options.callback(true);

        this.password = null;
        this.visible = false;
      } else {
        this.error = true;
      }
    }
  }
};
</script>

<style lang="less" scoped>
.modal-mask {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.5);
  margin-top: 0;
  margin-left: 0;
  align-items: center;
  justify-content: center;
  transition: opacity 0.3s ease;
  z-index: 9998;
}

.modal-container {
  max-width: 800px;
  max-height: 700px;
  width: calc(100% - 40px);
  height: auto;
  margin: 0px auto;
  background-color: #fff;
  box-shadow: 0 2px 10px rgba(0, 0, 0, 0.2);
  transition: all 0.3s ease;
}

.header {
  height: 56px;
  line-height: 56px;
  padding: 0 30px;
  border-bottom: 1px solid var(--main-border-color);
  font-weight: 600;
  font-size: 1.05rem;

  & .close {
    float: right;
    margin: 0 -10px 0 0;
  }

  & .icon {
    line-height: 42px;
    font-size: 1.2em;
    font-weight: 300;
    padding: 0 10px;
    cursor: pointer;
  }

  & .icon:hover {
    color: var(--primary-color);
    background: var(--hover-color);
  }
}

.content {
  margin: 30px 0;
  padding: 0 30px;
  overflow-y: auto;
}

.buttons {
  height: 64px;
  padding: 0 12px;
}
</style>

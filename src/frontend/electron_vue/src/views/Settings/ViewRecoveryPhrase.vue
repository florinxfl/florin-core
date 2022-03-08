<template>
  <div class="view-recovery-phrase-view">
    <div>
      <!-- If wallet is locked recovery phrase is null -->
      <content-wrapper v-if="!hasRecoveryPhrase" heading="common.important" heading-style="warning" content="Blah blaah blah bla blaah bla blaah">
        <app-section class="phrase">
          In order to see your recovery phrase you need to unlock your wallet
        </app-section>
      </content-wrapper>

      <!-- else show recovery phraase -->
      <content-wrapper v-else heading="common.important" heading-style="warning" content="setup.this_is_your_recovery_phrase">
        <app-section class="phrase">
          {{ recoveryPhrase }}
        </app-section>
      </content-wrapper>
    </div>

    <div class="flex-1"></div>

    <app-button-section>
      <button @click="toggleLocked">
        {{ $t(this.unlocked ? "buttons.lock" : "buttons.unlock") }}
      </button>
      <button @click="back">
        {{ $t("buttons.back") }}
      </button>
    </app-button-section>
  </div>
</template>

<script>
import { mapState } from "vuex";
import { LibraryController } from "../../unity/Controllers";
import EventBus from "../../EventBus";
import UIConfig from "../../../ui-config.json";

export default {
  data() {
    return {
      recoveryPhrase: null,
      UIConfig: UIConfig
    };
  },
  computed: {
    ...mapState("wallet", ["unlocked"]),
    hasRecoveryPhrase() {
      return this.unlocked && this.recoveryPhrase !== null;
    }
  },
  watch: {
    unlocked: {
      immediate: true,
      handler() {
        if (this.unlocked) {
          this.viewRecoveryPhrase();
        } else {
          this.recoveryPhrase = null;
        }
      }
    }
  },
  methods: {
    toggleLocked() {
      if (this.unlocked) {
        EventBus.$emit("lock-wallet");
      } else {
        this.viewRecoveryPhrase();
      }
    },
    viewRecoveryPhrase() {
      if (this.recoveryPhrase) return;
      EventBus.$emit("unlock-wallet", {
        callback: e => this.tryGetRecoveryPhrase(e),
        timeout: 60
      });
    },
    tryGetRecoveryPhrase(e) {
      if (!e) return;
      this.recoveryPhrase = LibraryController.GetRecoveryPhrase().phrase;
    },
    back() {
      this.$router.push({ name: "settings" });
    }
  }
};
</script>

<style lang="less" scoped>
.view-recovery-phrase-view {
  display: flex;
  height: 100%;
  flex-direction: column;
  flex-wrap: nowrap;
  justify-content: space-between;
}

.phrase {
  padding: 15px;
  font-size: 1.05em;
  font-weight: 500;
  text-align: center;
  word-spacing: 4px;
  background-color: #f5f5f5;
  user-select: none;
}
</style>

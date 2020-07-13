import Vue from "vue";
import VueRouter from "vue-router";
import Wallet from "../views/Wallet.vue";

Vue.use(VueRouter);

const routes = [
  {
    path: "/",
    name: "wallet",
    component: Wallet
  },
  {
    path: "/setup",
    name: "setup",
    component: () =>
      import(/* webpackChunkName: "setup" */ "../views/Setup.vue")
  },
  {
    path: "/send",
    name: "send",
    component: () => import(/* webpackChunkName: "send" */ "../views/Send.vue")
  },
  {
    path: "/history",
    name: "history",
    component: () =>
      import(/* webpackChunkName: "send" */ "../views/History.vue")
  },
  {
    path: "/settings",
    component: () =>
      import(/* webpackChunkName: "settings" */ "../views/Settings.vue"),
    children: [
      {
        path: "",
        name: "settings",
        component: () =>
          import(
            /* webpackChunkName: "settings-list" */ "../views/Settings/SettingsList.vue"
          )
      },
      {
        path: "view-recovery-phrase",
        name: "view-recovery-phrase",
        component: () =>
          import(
            /* webpackChunkName: "view-recovery-phrase" */ "../views/Settings/ViewRecoveryPhrase.vue"
          )
      },
      {
        path: "change-password",
        name: "change-password",
        component: () =>
          import(
            /* webpackChunkName: "change-password" */ "../views/Settings/ChangePassword.vue"
          )
      }
    ]
  },
  {
    path: "/debug",
    name: "debug",
    component: { template: "<div></div>" }
  }
];

const router = new VueRouter({
  routes
});

export default router;

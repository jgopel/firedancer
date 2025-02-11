use {
    solana_sdk::{
        signature::{Keypair},
    },
    solana_client::{
        rpc_client::{RpcClient},
    },
    std::{
        sync::Arc,
    },
};

use crate::bpf_loader;

/// CI Link: gs://firedancer-ci-resources/v18multi-bpf-loader.tar.gz
pub fn bpf_loader_ledger(client: &RpcClient, arc_client: &Arc<RpcClient>, payer: &Keypair, program_data: &Vec<u8>, account_data: &Vec<u8>) {
    bpf_loader::deploy_invoke_same_slot(&client, &arc_client, &payer, &program_data, &account_data);
    bpf_loader::deploy_invoke_diff_slot(&client, &arc_client, &payer, &program_data, &account_data);

    bpf_loader::upgrade_invoke_same_slot(&client, &arc_client, &payer, &program_data, &account_data);
    bpf_loader::upgrade_invoke_diff_slot(&client, &arc_client, &payer, &program_data, &account_data);

    bpf_loader::deploy_close_same_slot(&client, &arc_client, &payer, &program_data, &account_data);
    bpf_loader::deploy_close_diff_slot(&client, &arc_client, &payer, &program_data, &account_data);

    bpf_loader::close_invoke_same_slot(&client, &arc_client, &payer, &program_data, &account_data);
    bpf_loader::close_invoke_diff_slot(&client, &arc_client, &payer, &program_data, &account_data);

    bpf_loader::close_redeploy_same_slot(&client, &arc_client, &payer, &program_data, &account_data);
    bpf_loader::close_redeploy_diff_slot(&client, &arc_client, &payer, &program_data, &account_data);
}

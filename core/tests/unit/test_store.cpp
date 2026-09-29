#include "bundle/store.hpp"

#include <cassert>
#include <string>
#include <variant>

namespace {

bundle::CurrencyCode usd() {
    return *bundle::CurrencyCode::parse("USD");
}

bundle::SourceRef source() {
    return bundle::SourceRef{
        .source_id = "csv-import-1",
        .source_kind = bundle::SourceKind::csv,
        .provider_id = std::string{"local-file"},
        .external_object_id = std::string{"row-1"},
        .imported_at = bundle::UtcTimestamp::from_unix_millis(2'000),
        .observed_at = bundle::UtcTimestamp::from_unix_millis(1'000),
        .content_hash = "sha256:0000000000000000000000000000000000000000000000000000000000000000",
        .source_version = 1,
        .metadata = "fixture",
    };
}

bundle::Account account() {
    return bundle::Account{
        .account_id = "checking",
        .source_id = "csv-import-1",
        .display_name = "Checking",
        .institution_name = "Local fixture",
        .account_type = bundle::AccountType::checking,
        .account_subtype = "checking",
        .currency = usd(),
        .current_balance = bundle::Money{1'500, usd()},
        .available_balance = bundle::Money{1'500, usd()},
        .balance_observed_at = bundle::UtcTimestamp::from_unix_millis(1'000),
        .balance_freshness = bundle::BalanceFreshness::fresh,
        .last_four = std::nullopt,
        .status = bundle::AccountStatus::active,
        .include_in_safe_to_spend = true,
        .include_in_net_position = true,
    };
}

bundle::BalanceObservation balance(const std::string& id, const std::int64_t recorded_at) {
    return bundle::BalanceObservation{
        .observation_id = id,
        .account_id = "checking",
        .current_minor = 1'500,
        .available_minor = 1'500,
        .observed_at = bundle::UtcTimestamp::from_unix_millis(1'000),
        .recorded_at = bundle::UtcTimestamp::from_unix_millis(recorded_at),
        .source_ref = source(),
        .status = bundle::BalanceObservationStatus::valid,
    };
}

bundle::Transaction transaction(const std::uint64_t revision) {
    return bundle::Transaction{
        .transaction_id = "transaction-" + std::to_string(revision),
        .account_id = "checking",
        .source_transaction_id = "provider-row-1",
        .revision = revision,
        .amount = bundle::Money{250, usd()},
        .direction = bundle::TransactionDirection::debit,
        .transaction_state = revision == 1 ? bundle::TransactionState::pending
                                           : bundle::TransactionState::posted,
        .merchant_raw = "EXAMPLE",
        .merchant_id = std::nullopt,
        .description = "Example purchase",
        .category_id = std::nullopt,
        .authorized_at = std::nullopt,
        .posted_at = revision == 1
                         ? std::nullopt
                         : std::optional<bundle::UtcTimestamp>{
                               bundle::UtcTimestamp::from_unix_millis(3'000)},
        .observed_at = bundle::UtcTimestamp::from_unix_millis(3'000),
        .recurring_candidate_id = std::nullopt,
        .source_ref = source(),
        .supersedes_transaction_revision = revision == 1
                                               ? std::nullopt
                                               : std::optional<std::uint64_t>{1},
        .user_note = "",
        .user_tags = {},
    };
}

void append_only_history_and_events_are_preserved() {
    bundle::FinanceStore store;
    assert(std::holds_alternative<std::monostate>(store.append_source(source())));
    assert(std::holds_alternative<std::monostate>(store.append_account(account())));
    assert(std::holds_alternative<std::monostate>(store.append_balance_observation(balance("balance-1", 2'000))));
    assert(std::holds_alternative<std::monostate>(store.append_balance_observation(balance("balance-2", 3'000))));

    assert(store.balance_observations().size() == 2);
    assert(store.latest_balance("checking")->observation_id == "balance-2");
    assert(store.event_log().size() == 4);
    assert(store.event_log()[1].content_hash == source().content_hash);
    assert(store.event_log()[3].event_type == "balance_observation.appended");

    auto changed_balance = balance("balance-1", 2'000);
    changed_balance.available_minor = 1'400;
    assert(std::get<bundle::StoreError>(store.append_balance_observation(changed_balance)) ==
           bundle::StoreError::revision_conflict);
}

void transaction_replay_is_idempotent_and_revisioned() {
    bundle::FinanceStore store;
    assert(std::holds_alternative<std::monostate>(store.append_source(source())));
    assert(std::holds_alternative<std::monostate>(store.append_account(account())));

    auto changed_source = source();
    changed_source.metadata = "changed";
    assert(std::get<bundle::StoreError>(store.append_source(changed_source)) ==
           bundle::StoreError::revision_conflict);

    const auto first = transaction(1);
    assert(std::holds_alternative<std::monostate>(store.append_transaction(first)));
    const auto events_after_first = store.event_log().size();

    assert(std::get<bundle::StoreError>(store.append_transaction(first)) ==
           bundle::StoreError::duplicate_object);
    auto changed_first = first;
    changed_first.description = "Changed purchase";
    assert(std::get<bundle::StoreError>(store.append_transaction(changed_first)) ==
           bundle::StoreError::revision_conflict);
    assert(store.event_log().size() == events_after_first);

    auto revised = transaction(2);
    assert(std::holds_alternative<std::monostate>(store.append_transaction(revised)));
    assert(store.transactions().size() == 2);
    assert(store.latest_transaction("checking", "provider-row-1")->revision == 2);
    assert(store.event_log().back().content_hash == source().content_hash);

    auto reused_transaction_id = transaction(3);
    reused_transaction_id.transaction_id = "transaction-1";
    assert(std::get<bundle::StoreError>(store.append_transaction(reused_transaction_id)) ==
           bundle::StoreError::revision_conflict);
}

void missing_scope_and_revision_gaps_fail_closed() {
    bundle::FinanceStore store;
    assert(std::holds_alternative<std::monostate>(store.append_source(source())));
    auto invalid_account = account();
    invalid_account.source_id = "missing-source";
    assert(std::get<bundle::StoreError>(store.append_account(invalid_account)) ==
           bundle::StoreError::missing_source);

    assert(std::holds_alternative<std::monostate>(store.append_account(account())));
    auto changed_account = account();
    changed_account.display_name = "Changed checking";
    assert(std::get<bundle::StoreError>(store.append_account(changed_account)) ==
           bundle::StoreError::revision_conflict);

    auto gap = transaction(2);
    assert(std::get<bundle::StoreError>(store.append_transaction(gap)) ==
           bundle::StoreError::revision_gap);

    auto unknown_source_version = balance("balance-version-2", 2'000);
    unknown_source_version.source_ref.source_version = 2;
    assert(std::get<bundle::StoreError>(store.append_balance_observation(unknown_source_version)) ==
           bundle::StoreError::missing_source);

    auto tampered_balance = balance("balance-tampered", 2'000);
    tampered_balance.source_ref.metadata = "tampered";
    assert(std::get<bundle::StoreError>(store.append_balance_observation(tampered_balance)) ==
           bundle::StoreError::revision_conflict);

    auto tampered_transaction = transaction(1);
    tampered_transaction.source_ref.metadata = "tampered";
    assert(std::get<bundle::StoreError>(store.append_transaction(tampered_transaction)) ==
           bundle::StoreError::revision_conflict);
}

void calculation_receipts_are_append_only_and_audited() {
    bundle::FinanceStore store;
    const auto receipt = bundle::CalculationReceipt{
        .receipt_id = "receipt-1",
        .engine = "forecast",
        .engine_version = "0.1.0",
        .calculated_at = bundle::UtcTimestamp::from_unix_millis(10),
        .input_refs = {"account:checking", "commitment:rent"},
        .input_hash = "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .policy_id = "expected",
        .policy_version = "1",
        .outputs_json = "{\"safe_to_spend_minor\":300}",
    };
    assert(std::holds_alternative<std::monostate>(store.append_calculation_receipt(receipt)));
    const auto duplicate = store.append_calculation_receipt(receipt);
    assert(std::holds_alternative<bundle::StoreError>(duplicate));
    assert(std::get<bundle::StoreError>(duplicate) == bundle::StoreError::duplicate_object);
    assert(store.calculation_receipts().size() == 1);
    assert(store.event_log().back().event_type == "calculation_receipt.appended");

    auto invalid = receipt;
    invalid.receipt_id = "receipt-2";
    invalid.input_hash = "not-a-hash";
    const auto invalid_result = store.append_calculation_receipt(invalid);
    assert(std::holds_alternative<bundle::StoreError>(invalid_result));
    assert(std::get<bundle::StoreError>(invalid_result) ==
           bundle::StoreError::invalid_calculation_receipt);

    auto invalid_json = receipt;
    invalid_json.receipt_id = "receipt-3";
    invalid_json.outputs_json = "{\"safe_to_spend_minor\":}";
    const auto invalid_json_result = store.append_calculation_receipt(invalid_json);
    assert(std::holds_alternative<bundle::StoreError>(invalid_json_result));
    assert(std::get<bundle::StoreError>(invalid_json_result) ==
           bundle::StoreError::invalid_calculation_receipt);
}

}  // namespace

int main() {
    append_only_history_and_events_are_preserved();
    transaction_replay_is_idempotent_and_revisioned();
    missing_scope_and_revision_gaps_fail_closed();
    calculation_receipts_are_append_only_and_audited();
    return 0;
}

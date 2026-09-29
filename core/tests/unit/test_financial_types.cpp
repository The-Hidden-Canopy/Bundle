#include "bundle/financial.hpp"
#include "bundle/money.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>

namespace {

bundle::CurrencyCode currency(const std::string& value) {
    const auto parsed = bundle::CurrencyCode::parse(value);
    assert(parsed.has_value());
    return *parsed;
}

bundle::SourceRef source_ref() {
    return bundle::SourceRef{
        .source_id = "import-1",
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

void money_rules_are_integer_and_currency_bound() {
    const auto usd = currency("USD");
    const auto jpy = currency("JPY");
    const bundle::Money twelve_dollars{1'200, usd};
    const bundle::Money three_dollars{300, usd};

    const auto sum = bundle::add(twelve_dollars, three_dollars);
    assert(std::holds_alternative<bundle::Money>(sum));
    assert(std::get<bundle::Money>(sum).minor_units == 1'500);

    const auto mismatch = bundle::add(twelve_dollars, bundle::Money{300, jpy});
    assert(std::get<bundle::MoneyError>(mismatch) ==
           bundle::MoneyError::currency_mismatch);

    const auto overflow = bundle::add(
        bundle::Money{std::numeric_limits<std::int64_t>::max(), usd},
        bundle::Money{1, usd});
    assert(std::get<bundle::MoneyError>(overflow) == bundle::MoneyError::overflow);

    assert(bundle::currency_exponent(usd) == 2);
    assert(bundle::currency_exponent(jpy) == 0);
    assert(!bundle::CurrencyCode::parse("usd").has_value());
    assert(!bundle::CurrencyCode::parse("US").has_value());
}

void provenance_and_utc_boundary_are_required() {
    auto source = source_ref();
    assert(!bundle::validate_source_ref(source).has_value());

    source.content_hash.clear();
    assert(bundle::validate_source_ref(source) ==
           bundle::FinancialError::invalid_content_hash);

    source.content_hash = "sha256:example";
    assert(bundle::validate_source_ref(source) ==
           bundle::FinancialError::invalid_content_hash);

    source.content_hash = "sha256:0000000000000000000000000000000000000000000000000000000000000000";
    source.source_version = 0;
    assert(bundle::validate_source_ref(source) ==
           bundle::FinancialError::invalid_source_version);
}

void account_rejects_currency_drift_and_invalid_last_four() {
    const auto usd = currency("USD");
    const auto jpy = currency("JPY");
    auto account = bundle::Account{
        .account_id = "account-1",
        .source_id = "source-1",
        .display_name = "Checking",
        .institution_name = "Local fixture",
        .account_type = bundle::AccountType::checking,
        .account_subtype = "checking",
        .currency = usd,
        .current_balance = bundle::Money{1'000, usd},
        .available_balance = bundle::Money{900, usd},
        .balance_observed_at = bundle::UtcTimestamp::from_unix_millis(1'000),
        .balance_freshness = bundle::BalanceFreshness::fresh,
        .last_four = std::string{"1234"},
        .status = bundle::AccountStatus::active,
        .include_in_safe_to_spend = true,
        .include_in_net_position = true,
    };
    assert(!bundle::validate_account(account).has_value());

    account.available_balance.currency = jpy;
    assert(bundle::validate_account(account) == bundle::FinancialError::currency_mismatch);

    account.available_balance.currency = usd;
    account.last_four = "12";
    assert(bundle::validate_account(account) == bundle::FinancialError::invalid_last_four);
}

void observations_and_revisions_preserve_history_boundaries() {
    const auto source = source_ref();
    auto observation = bundle::BalanceObservation{
        .observation_id = "balance-1",
        .account_id = "account-1",
        .current_minor = 1'000,
        .available_minor = 900,
        .observed_at = bundle::UtcTimestamp::from_unix_millis(1'000),
        .recorded_at = bundle::UtcTimestamp::from_unix_millis(2'000),
        .source_ref = source,
        .status = bundle::BalanceObservationStatus::stale,
    };
    assert(!bundle::validate_balance_observation(observation).has_value());

    observation.recorded_at = bundle::UtcTimestamp::from_unix_millis(900);
    assert(bundle::validate_balance_observation(observation) ==
           bundle::FinancialError::invalid_timestamp_order);

    auto transaction = bundle::Transaction{
        .transaction_id = "transaction-1",
        .account_id = "account-1",
        .source_transaction_id = "row-1",
        .revision = 2,
        .amount = bundle::Money{250, currency("USD")},
        .direction = bundle::TransactionDirection::debit,
        .transaction_state = bundle::TransactionState::posted,
        .merchant_raw = "EXAMPLE",
        .merchant_id = std::nullopt,
        .description = "Example purchase",
        .category_id = std::nullopt,
        .authorized_at = std::nullopt,
        .posted_at = bundle::UtcTimestamp::from_unix_millis(2'000),
        .observed_at = bundle::UtcTimestamp::from_unix_millis(2'000),
        .recurring_candidate_id = std::nullopt,
        .source_ref = source,
        .supersedes_transaction_revision = 1,
        .user_note = "",
        .user_tags = {},
    };
    assert(!bundle::validate_transaction(transaction).has_value());

    transaction.supersedes_transaction_revision = 2;
    assert(bundle::validate_transaction(transaction) ==
           bundle::FinancialError::invalid_superseded_revision);
}

}  // namespace

int main() {
    money_rules_are_integer_and_currency_bound();
    provenance_and_utc_boundary_are_required();
    account_rejects_currency_drift_and_invalid_last_four();
    observations_and_revisions_preserve_history_boundaries();
    return 0;
}

-- Sachin Dev Loader v1: final schema.
--
--   profiles           one row per loader account (bound to auth.users)
--   loader_config      one row: maintenance switch and release versions
--   loader_components  content released to the client
--
-- Ship a release by publishing the GitHub release, then editing version in
-- loader_config:
--   update public.loader_config set version = '2' where id = 1;
-- The Edge Function finds the matching GitHub release (tag v2) and sends
-- its download URL and SHA-256 to the client. A build below this version
-- updates itself on the loading screen.
--
-- Safe to re-run. Account rows, passwords and hardware bindings are kept.

create extension if not exists pgcrypto with schema extensions;

-- ---------------------------------------------------------------- tables ----

create table if not exists public.profiles (
  id uuid primary key references auth.users(id) on delete cascade,
  username text not null unique,
  is_banned boolean not null default false,
  hwid text,
  hwid_bound_at timestamptz,
  last_login_at timestamptz,
  login_count integer not null default 0,
  expires_at timestamptz,
  created_at timestamptz not null default now()
);

alter table public.profiles add column if not exists expires_at timestamptz;
alter table public.profiles drop column if exists updated_at;

do $$
begin
  if not exists (
    select 1 from pg_constraint
    where conname = 'profiles_username_format'
      and conrelid = 'public.profiles'::regclass
  ) then
    alter table public.profiles
      add constraint profiles_username_format check (username ~ '^[a-z0-9_]{2,20}$');
  end if;
end;
$$;

create table if not exists public.loader_config (
  id smallint primary key default 1,
  maintenance boolean not null default false,
  maintenance_message text not null default '',
  version text not null default '1',
  constraint loader_config_single_row check (id = 1)
);

alter table public.loader_config add column if not exists version text not null default '1';
do $$
begin
  if exists (select 1 from information_schema.columns
             where table_schema = 'public' and table_name = 'loader_config' and column_name = 'latest_version') then
    update public.loader_config set version = latest_version where id = 1;
  end if;
end;
$$;
alter table public.loader_config drop column if exists min_version;
alter table public.loader_config drop column if exists latest_version;

alter table public.loader_config drop column if exists update_url;
alter table public.loader_config drop column if exists update_sha256;
alter table public.loader_config drop column if exists release_url;
alter table public.loader_config drop column if exists announcement;
alter table public.loader_config drop column if exists announcement_level;
alter table public.loader_config drop column if exists support_url;
alter table public.loader_config drop column if exists auto_sync;
alter table public.loader_config drop column if exists release_checked_at;
alter table public.loader_config drop column if exists updated_at;

create table if not exists public.loader_components (
  id bigint generated always as identity primary key,
  name text not null unique,
  version text not null,
  description text not null default '',
  download_url text not null default '',
  sha256 text not null default '',
  status text not null default 'online',
  requires_login boolean not null default true,
  sort_order integer not null default 0
);

alter table public.loader_components add column if not exists download_url text not null default '';
alter table public.loader_components add column if not exists sha256 text not null default '';
alter table public.loader_components add column if not exists status text not null default 'online';

-- A disabled component is the same as an offline one.
do $$
begin
  if exists (select 1 from information_schema.columns
             where table_schema = 'public' and table_name = 'loader_components' and column_name = 'enabled') then
    update public.loader_components set status = 'offline' where not enabled;
  end if;
end;
$$;

alter table public.loader_components drop column if exists enabled;
alter table public.loader_components drop column if exists created_at;
alter table public.loader_components drop column if exists updated_at;

do $$
begin
  if not exists (select 1 from pg_constraint where conname = 'loader_components_status'
                 and conrelid = 'public.loader_components'::regclass) then
    alter table public.loader_components
      add constraint loader_components_status check (status in ('online', 'updating', 'offline', 'beta'));
  end if;

  if not exists (select 1 from pg_constraint where conname = 'loader_components_download'
                 and conrelid = 'public.loader_components'::regclass) then
    alter table public.loader_components
      add constraint loader_components_download check (
        download_url = '' or (download_url ~ '^https://' and sha256 ~ '^[0-9a-f]{64}$')
      );
  end if;
end;
$$;

drop table if exists public.loader_news cascade;
drop table if exists public.loader_builds cascade;
drop table if exists public.loader_check_log cascade;

-- ------------------------------------------------------------- seed data ----

insert into public.loader_config (id, maintenance, maintenance_message, version)
values (1, false, '', '1')
on conflict (id) do nothing;

insert into public.loader_components (name, version, description, status, requires_login, sort_order)
values ('Core', '1', 'Loader core and session tools', 'online', true, 10)
on conflict (name) do update set
  version = excluded.version,
  description = excluded.description;

-- Rebuild profile rows for existing loader accounts.
insert into public.profiles (id, username)
select u.id, split_part(lower(u.email), '@', 1)
from auth.users u
where lower(u.email) like '%@loader.local'
  and split_part(lower(u.email), '@', 1) ~ '^[a-z0-9_]{2,20}$'
on conflict (id) do nothing;

-- ---------------------------------------------------------- old functions ----

-- Every loader function is rebuilt below, so drop all earlier versions first.
-- This also clears functions of removed features (news, announcements, sync).
do $$
declare
  f record;
begin
  for f in
    select p.oid::regprocedure as sig
    from pg_proc p
    where p.pronamespace = 'public'::regnamespace
      and (p.proname like 'loader\_%' or p.proname like 'account\_%' or p.proname like 'admin\_%')
  loop
    execute format('drop function if exists %s cascade', f.sig);
  end loop;
end;
$$;

-- -------------------------------------------------------------- helpers ----

create or replace function public.loader_version_parts(p_version text)
returns integer[]
language sql
immutable
set search_path = ''
as $$
  select case
    when coalesce(p_version, '') ~ '^[0-9]{1,4}([.][0-9]{1,4}){0,3}$'
      then string_to_array(p_version, '.')::integer[]
    else null
  end;
$$;

create or replace function public.loader_version_cmp(p_left text, p_right text)
returns integer
language plpgsql
immutable
set search_path = ''
as $$
declare
  v_left integer[] := public.loader_version_parts(p_left);
  v_right integer[] := public.loader_version_parts(p_right);
  i integer;
  a integer;
  b integer;
begin
  if v_left is null or v_right is null then
    return null;
  end if;

  for i in 1..4 loop
    a := coalesce(v_left[i], 0);
    b := coalesce(v_right[i], 0);
    if a < b then return -1; end if;
    if a > b then return 1; end if;
  end loop;

  return 0;
end;
$$;

create or replace function public.loader_update_info()
returns jsonb
language sql
stable
security definer
set search_path = ''
as $$
  select jsonb_build_object('version', c.version)
  from public.loader_config c
  where c.id = 1;
$$;

-- Download data is only released to signed-in accounts.
create or replace function public.loader_components_json(p_authenticated boolean)
returns jsonb
language sql
stable
security definer
set search_path = ''
as $$
  select coalesce(
    jsonb_agg(
      jsonb_build_object(
        'name', c.name,
        'version', c.version,
        'description', c.description,
        'status', c.status,
        'download_url', case when p_authenticated then c.download_url else '' end,
        'sha256', case when p_authenticated then c.sha256 else '' end
      )
      order by c.sort_order, c.name
    ),
    '[]'::jsonb
  )
  from public.loader_components c
  where p_authenticated or not c.requires_login;
$$;

-- ---------------------------------------------------------- build checks ----

-- Returns null when the build may continue, otherwise the error to send.
create or replace function public.loader_check_build(p_version text, p_exe_hash text)
returns jsonb
language plpgsql
stable
security definer
set search_path = ''
as $$
declare
  v_config public.loader_config%rowtype;
  v_cmp integer;
begin
  select * into v_config from public.loader_config where id = 1;
  if not found then
    return jsonb_build_object('ok', false, 'error', 'config_missing',
      'message', 'Loader configuration is missing');
  end if;

  if v_config.maintenance then
    return jsonb_build_object('ok', false, 'error', 'maintenance',
      'message', coalesce(nullif(v_config.maintenance_message, ''), 'Loader is under maintenance. Try again later'));
  end if;

  if public.loader_version_parts(p_version) is null then
    return jsonb_build_object('ok', false, 'error', 'bad_build',
      'message', 'Loader version is not valid');
  end if;

  v_cmp := public.loader_version_cmp(p_version, v_config.version);
  if v_cmp is null or v_cmp < 0 then
    return jsonb_build_object('ok', false, 'error', 'outdated',
      'message', 'This loader version is outdated. Download v' || v_config.version);
  end if;

  if p_exe_hash is null or p_exe_hash !~ '^[0-9a-f]{64}$' then
    return jsonb_build_object('ok', false, 'error', 'bad_build',
      'message', 'Loader file is modified or unknown. Download the official build');
  end if;

  return null;
end;
$$;

-- --------------------------------------------------------------- account ----

create or replace function public.account_login(p_hwid text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_uid uuid := auth.uid();
  v_hwid text := upper(trim(coalesce(p_hwid, '')));
  v_profile public.profiles%rowtype;
begin
  if v_uid is null then
    return jsonb_build_object('ok', false, 'error', 'not_authenticated');
  end if;

  select * into v_profile from public.profiles p where p.id = v_uid;
  if not found then
    return jsonb_build_object('ok', false, 'error', 'no_account');
  end if;

  if v_profile.is_banned then
    return jsonb_build_object('ok', false, 'error', 'banned');
  end if;

  if v_profile.expires_at is not null and v_profile.expires_at <= now() then
    return jsonb_build_object('ok', false, 'error', 'expired');
  end if;

  if v_hwid = '' then
    return jsonb_build_object('ok', false, 'error', 'invalid_input');
  end if;

  if v_profile.hwid is null then
    update public.profiles
    set hwid = v_hwid, hwid_bound_at = now(), last_login_at = now(), login_count = login_count + 1
    where id = v_uid;
  elsif upper(v_profile.hwid) <> v_hwid then
    return jsonb_build_object('ok', false, 'error', 'hwid_mismatch');
  else
    update public.profiles
    set last_login_at = now(), login_count = login_count + 1
    where id = v_uid;
  end if;

  return jsonb_build_object('ok', true, 'username', v_profile.username, 'hwid', v_hwid);
end;
$$;

create or replace function public.account_status(p_hwid text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_uid uuid := auth.uid();
  v_hwid text := upper(trim(coalesce(p_hwid, '')));
  v_profile public.profiles%rowtype;
begin
  if v_uid is null then
    return jsonb_build_object('ok', false, 'error', 'not_authenticated');
  end if;

  select * into v_profile from public.profiles p where p.id = v_uid;
  if not found then
    return jsonb_build_object('ok', false, 'error', 'no_account');
  end if;

  if v_profile.is_banned then
    return jsonb_build_object('ok', false, 'error', 'banned');
  end if;

  if v_profile.expires_at is not null and v_profile.expires_at <= now() then
    return jsonb_build_object('ok', false, 'error', 'expired');
  end if;

  if v_profile.hwid is not null and upper(v_profile.hwid) <> v_hwid then
    return jsonb_build_object('ok', false, 'error', 'hwid_mismatch');
  end if;

  return jsonb_build_object(
    'ok', true,
    'username', v_profile.username,
    'hwid', v_profile.hwid,
    'expires_at', coalesce(extract(epoch from v_profile.expires_at)::bigint, 0)
  );
end;
$$;

-- ------------------------------------------------------------- bootstrap ----

create or replace function public.loader_startup(p_hwid text, p_version text, p_exe_hash text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_error jsonb;
  v_info jsonb := public.loader_update_info();
begin
  v_error := public.loader_check_build(p_version, p_exe_hash);

  if v_error is not null then
    if v_error ->> 'error' in ('maintenance', 'config_missing') then
      return v_error;
    end if;
    return v_error || v_info;
  end if;

  return jsonb_build_object('ok', true, 'components', public.loader_components_json(false)) || v_info;
end;
$$;

create or replace function public.loader_bootstrap(p_hwid text, p_version text, p_exe_hash text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_status jsonb;
  v_error jsonb;
  v_info jsonb := public.loader_update_info();
begin
  if auth.uid() is null then
    return jsonb_build_object('ok', false, 'error', 'not_authenticated');
  end if;

  v_status := public.account_status(p_hwid);

  if coalesce((v_status ->> 'ok')::boolean, false) is not true then
    v_error := jsonb_build_object('ok', false, 'error', coalesce(v_status ->> 'error', 'not_authenticated'));
  else
    v_error := public.loader_check_build(p_version, p_exe_hash);
  end if;

  if v_error is not null then
    if v_error ->> 'error' in ('maintenance', 'config_missing') then
      return v_error;
    end if;
    return v_error || v_info;
  end if;

  return jsonb_build_object(
    'ok', true,
    'username', v_status ->> 'username',
    'expires_at', coalesce((v_status ->> 'expires_at')::bigint, 0),
    'components', public.loader_components_json(true)
  ) || v_info;
end;
$$;

-- ------------------------------------------------------------------ admin ----

create or replace function public.admin_create_account(p_username text, p_password text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_username text := lower(trim(coalesce(p_username, '')));
  v_email text;
  v_uid uuid := gen_random_uuid();
begin
  if v_username !~ '^[a-z0-9_]{2,20}$' then
    return jsonb_build_object('ok', false, 'error', 'invalid_username');
  end if;

  if length(coalesce(p_password, '')) < 2 then
    return jsonb_build_object('ok', false, 'error', 'weak_password');
  end if;

  v_email := v_username || '@loader.local';

  if exists (select 1 from auth.users u where lower(u.email) = v_email) then
    return jsonb_build_object('ok', false, 'error', 'username_taken');
  end if;

  insert into auth.users (
    instance_id, id, aud, role, email, encrypted_password, email_confirmed_at,
    confirmation_token, recovery_token, email_change, email_change_token_new, email_change_token_current,
    phone_change, phone_change_token, reauthentication_token,
    raw_app_meta_data, raw_user_meta_data, created_at, updated_at
  ) values (
    '00000000-0000-0000-0000-000000000000', v_uid, 'authenticated', 'authenticated', v_email,
    extensions.crypt(p_password, extensions.gen_salt('bf', 10)), now(),
    '', '', '', '', '',
    '', '', '',
    '{"provider":"email","providers":["email"]}'::jsonb,
    jsonb_build_object('username', v_username, 'email_verified', true, 'phone_verified', false),
    now(), now()
  );

  insert into auth.identities (id, user_id, provider_id, identity_data, provider, last_sign_in_at, created_at, updated_at)
  values (
    gen_random_uuid(), v_uid, v_uid::text,
    jsonb_build_object('sub', v_uid::text, 'email', v_email, 'email_verified', true, 'phone_verified', false),
    'email', now(), now(), now()
  );

  insert into public.profiles (id, username) values (v_uid, v_username);

  return jsonb_build_object('ok', true, 'username', v_username, 'email', v_email, 'user_id', v_uid);
exception when unique_violation then
  return jsonb_build_object('ok', false, 'error', 'conflict');
end;
$$;

create or replace function public.admin_set_password(p_username text, p_password text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_email text := lower(trim(coalesce(p_username, ''))) || '@loader.local';
  v_count integer;
begin
  if length(coalesce(p_password, '')) < 2 then
    return jsonb_build_object('ok', false, 'error', 'weak_password');
  end if;

  update auth.users
  set encrypted_password = extensions.crypt(p_password, extensions.gen_salt('bf', 10)),
      updated_at = now()
  where lower(email) = v_email;
  get diagnostics v_count = row_count;

  return jsonb_build_object('ok', v_count > 0, 'updated', v_count);
end;
$$;

create or replace function public.admin_reset_hwid(p_username text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_count integer;
begin
  update public.profiles
  set hwid = null, hwid_bound_at = null
  where username = lower(trim(coalesce(p_username, '')));
  get diagnostics v_count = row_count;

  return jsonb_build_object('ok', v_count > 0, 'updated', v_count);
end;
$$;

create or replace function public.admin_set_banned(p_username text, p_banned boolean)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_count integer;
begin
  update public.profiles
  set is_banned = coalesce(p_banned, false)
  where username = lower(trim(coalesce(p_username, '')));
  get diagnostics v_count = row_count;

  return jsonb_build_object('ok', v_count > 0, 'updated', v_count);
end;
$$;

-- Days from now; null clears the expiry (lifetime). 0 expires at once.
create or replace function public.admin_set_expiry(p_username text, p_days integer)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_count integer;
begin
  update public.profiles
  set expires_at = case when p_days is null then null else now() + make_interval(days => p_days) end
  where username = lower(trim(coalesce(p_username, '')));
  get diagnostics v_count = row_count;

  return jsonb_build_object('ok', v_count > 0, 'updated', v_count);
end;
$$;

create or replace function public.admin_extend_expiry(p_username text, p_days integer)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_count integer;
begin
  update public.profiles
  set expires_at = greatest(coalesce(expires_at, now()), now()) + make_interval(days => p_days)
  where username = lower(trim(coalesce(p_username, '')));
  get diagnostics v_count = row_count;

  return jsonb_build_object('ok', v_count > 0, 'updated', v_count);
end;
$$;

create or replace function public.admin_list_accounts()
returns table (
  username text,
  is_banned boolean,
  created_at timestamptz,
  last_login_at timestamptz,
  bound_hwid text,
  login_count integer,
  expires_at timestamptz
)
language sql
security definer
set search_path = ''
as $$
  select p.username, p.is_banned, p.created_at, p.last_login_at, p.hwid, p.login_count, p.expires_at
  from public.profiles p
  order by p.created_at desc;
$$;

-- Creates or updates a component. An empty download url lists it without a
-- launch button.
create or replace function public.admin_upsert_component(
  p_name text,
  p_version text,
  p_description text default '',
  p_download_url text default '',
  p_sha256 text default '',
  p_status text default 'online',
  p_requires_login boolean default true,
  p_sort_order integer default 100
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_name text := trim(coalesce(p_name, ''));
  v_sha text := lower(trim(coalesce(p_sha256, '')));
  v_url text := trim(coalesce(p_download_url, ''));
begin
  if v_name = '' or length(v_name) > 40 then
    return jsonb_build_object('ok', false, 'error', 'invalid_name');
  end if;
  if p_status not in ('online', 'updating', 'offline', 'beta') then
    return jsonb_build_object('ok', false, 'error', 'invalid_status');
  end if;
  if v_url <> '' and (v_url !~ '^https://' or v_sha !~ '^[0-9a-f]{64}$') then
    return jsonb_build_object('ok', false, 'error', 'invalid_download');
  end if;

  insert into public.loader_components (name, version, description, download_url, sha256, status, requires_login, sort_order)
  values (v_name, trim(coalesce(p_version, '')), coalesce(p_description, ''), v_url,
          case when v_url = '' then '' else v_sha end, p_status, coalesce(p_requires_login, true),
          coalesce(p_sort_order, 100))
  on conflict (name) do update set
    version = excluded.version,
    description = excluded.description,
    download_url = excluded.download_url,
    sha256 = excluded.sha256,
    status = excluded.status,
    requires_login = excluded.requires_login,
    sort_order = excluded.sort_order;

  return jsonb_build_object('ok', true, 'name', v_name);
end;
$$;

create or replace function public.admin_set_component_status(p_name text, p_status text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_count integer;
begin
  if p_status not in ('online', 'updating', 'offline', 'beta') then
    return jsonb_build_object('ok', false, 'error', 'invalid_status');
  end if;

  update public.loader_components set status = p_status where name = trim(coalesce(p_name, ''));
  get diagnostics v_count = row_count;

  return jsonb_build_object('ok', v_count > 0, 'updated', v_count);
end;
$$;

create or replace function public.admin_remove_component(p_name text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_count integer;
begin
  delete from public.loader_components where name = trim(coalesce(p_name, ''));
  get diagnostics v_count = row_count;

  return jsonb_build_object('ok', v_count > 0, 'deleted', v_count);
end;
$$;

-- ---------------------------------------------------------------- grants ----

alter table public.profiles enable row level security;
alter table public.loader_config enable row level security;
alter table public.loader_components enable row level security;

revoke all on public.profiles from anon, authenticated;
revoke all on public.loader_config from anon, authenticated;
revoke all on public.loader_components from anon, authenticated;
grant all on public.profiles to service_role;
grant all on public.loader_config to service_role;
grant all on public.loader_components to service_role;
grant usage, select on all sequences in schema public to service_role;

do $$
declare
  f record;
begin
  for f in
    select p.oid::regprocedure as sig
    from pg_proc p
    where p.pronamespace = 'public'::regnamespace
      and (p.proname like 'loader\_%' or p.proname like 'account\_%' or p.proname like 'admin\_%')
  loop
    execute format('revoke all on function %s from public, anon, authenticated', f.sig);
    execute format('grant execute on function %s to service_role', f.sig);
  end loop;
end;
$$;

grant execute on function public.loader_startup(text, text, text) to anon, authenticated, service_role;
grant execute on function public.loader_bootstrap(text, text, text) to authenticated, service_role;
grant execute on function public.account_login(text) to authenticated, service_role;
grant execute on function public.account_status(text) to authenticated, service_role;

-- Cloth Changer v1: app management schema.
--
--   clothchange_config     one row: app version, force-update floor,
--                          maintenance switch, dashboard notice and the
--                          self-update download target
--   clothchange_features   named on/off toggles the client reads at runtime
--   clothchange_items      the skin / cloth catalog pushed from the server
--
-- This layers on top of the loader schema (profiles, loader_config,
-- loader_components) and does not modify it. Accounts, hardware bindings and
-- the loader's own update pipeline keep working unchanged.
--
-- Ship a Cloth Changer release by publishing the GitHub release (tag v2), then:
--   select public.admin_set_clothchange_version('2');
-- The client compares its build against clothchange_config.version and
-- updates itself with download_url + sha256. Set min_version to hard-block
-- every older build on the loading screen.
--
-- Safe to re-run. Existing rows are kept; only missing objects are created.

-- ------------------------------------------------------------- tables ----

create table if not exists public.clothchange_config (
  id smallint primary key default 1,
  version text not null default '1',
  min_version text,
  maintenance boolean not null default false,
  maintenance_message text not null default '',
  notice text not null default '',
  notice_level text not null default 'info',
  download_url text not null default '',
  sha256 text not null default '',
  updated_at timestamptz not null default now(),
  constraint clothchange_config_single_row check (id = 1),
  constraint clothchange_config_notice_level check (notice_level in ('info', 'warning', 'danger')),
  constraint clothchange_config_download check (
    download_url = '' or (download_url ~ '^https://' and sha256 ~ '^[0-9a-f]{64}$')
  )
);

create table if not exists public.clothchange_features (
  key text primary key,
  enabled boolean not null default false,
  description text not null default ''
);

create table if not exists public.clothchange_items (
  item_id bigint primary key,
  name text not null,
  category text not null default 'other',
  rarity text not null default 'WHITE',
  icon text not null default '',
  enabled boolean not null default true,
  sort_order integer not null default 0,
  constraint clothchange_items_category check (
    category in ('top', 'bottom', 'shoes', 'head', 'mask', 'facepaint', 'traje', 'other')
  )
);

alter table public.clothchange_config add column if not exists min_version text;

-- ------------------------------------------------------------ seed data ----

insert into public.clothchange_config (id, version, maintenance, maintenance_message, notice, notice_level)
values (1, '1', false, '', '', 'info')
on conflict (id) do nothing;

insert into public.clothchange_features (key, enabled, description) values
  ('lobby_changer',  true,  'Change skins while in the lobby'),
  ('match_changer',  true,  'Change skins during a match'),
  ('auto_commit',    false, 'Re-apply the outfit automatically after respawn')
on conflict (key) do nothing;

-- Example catalog rows. Replace or extend from clothes.json; the client reads
-- whatever is enabled here. item_id is the in-game Free Fire item id.
insert into public.clothchange_items (item_id, name, category, rarity, icon, sort_order) values
  (101, 'Example Top',    'top',    'RED',   '', 10),
  (102, 'Example Bottom', 'bottom', 'RED',   '', 20),
  (103, 'Example Shoes',  'shoes',  'PURPLE','', 30)
on conflict (item_id) do nothing;

-- ------------------------------------------------------- version helpers ----
--
-- Reuse the loader's semantic-version helpers. They are created by the loader
-- migration; guard so this file still applies on a fresh database that has not
-- run it yet.

do $$
begin
  if to_regprocedure('public.loader_version_parts(text)') is null then
    create function public.loader_version_parts(p_version text)
    returns integer[]
    language sql immutable set search_path = ''
    as $fn1$
      select case
        when coalesce(p_version, '') ~ '^[0-9]{1,4}([.][0-9]{1,4}){0,3}$'
          then string_to_array(p_version, '.')::integer[]
        else null
      end;
    $fn1$;
  end if;

  if to_regprocedure('public.loader_version_cmp(text, text)') is null then
    create function public.loader_version_cmp(p_left text, p_right text)
    returns integer
    language plpgsql immutable set search_path = ''
    as $fn2$
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
    $fn2$;
  end if;
end;
$$;

-- --------------------------------------------------------------- reads ----

-- One call the client makes on the loading screen. Returns the app version,
-- whether this build must update, the maintenance state, the dashboard
-- notice and every feature toggle. The download target is only released to a
-- signed-in account.
create or replace function public.clothchange_bootstrap(p_version text, p_authenticated boolean default false)
returns jsonb
language plpgsql
stable
security definer
set search_path = ''
as $$
declare
  v_config public.clothchange_config%rowtype;
  v_min_cmp integer;
  v_cur_cmp integer;
begin
  select * into v_config from public.clothchange_config where id = 1;
  if not found then
    return jsonb_build_object('ok', false, 'error', 'config_missing',
      'message', 'Cloth Changer configuration is missing');
  end if;

  if v_config.maintenance then
    return jsonb_build_object('ok', false, 'error', 'maintenance',
      'message', coalesce(nullif(v_config.maintenance_message, ''),
        'Cloth Changer is under maintenance. Try again later'));
  end if;

  -- Hard floor: anything below min_version is blocked with a forced update.
  if v_config.min_version is not null and public.loader_version_parts(v_config.min_version) is not null then
    v_min_cmp := public.loader_version_cmp(coalesce(p_version, ''), v_config.min_version);
    if v_min_cmp is null or v_min_cmp < 0 then
      return jsonb_build_object('ok', false, 'error', 'outdated',
        'target', v_config.version,
        'download_url', case when p_authenticated then v_config.download_url else '' end,
        'sha256', case when p_authenticated then v_config.sha256 else '' end,
        'message', 'This build is blocked. Download v' || v_config.version);
    end if;
  end if;

  v_cur_cmp := public.loader_version_cmp(coalesce(p_version, '0'), v_config.version);

  return jsonb_build_object(
    'ok', true,
    'version', v_config.version,
    'update_available', coalesce(v_cur_cmp is not null and v_cur_cmp < 0, false),
    'min_version', coalesce(v_config.min_version, ''),
    'notice', v_config.notice,
    'notice_level', v_config.notice_level,
    'download_url', case when p_authenticated then v_config.download_url else '' end,
    'sha256', case when p_authenticated then v_config.sha256 else '' end,
    'features', coalesce(
      (select jsonb_object_agg(f.key, f.enabled) from public.clothchange_features f),
      '{}'::jsonb)
  );
end;
$$;

-- The enabled skin catalog. Only released to signed-in accounts.
create or replace function public.clothchange_catalog(p_authenticated boolean default false)
returns jsonb
language sql
stable
security definer
set search_path = ''
as $$
  select case
    when p_authenticated then coalesce(
      jsonb_agg(
        jsonb_build_object(
          'item_id', i.item_id,
          'name', i.name,
          'category', i.category,
          'rarity', i.rarity,
          'icon', i.icon
        ) order by i.sort_order, i.item_id
      ),
      '[]'::jsonb)
    else '[]'::jsonb
  end
  from public.clothchange_items i
  where i.enabled;
$$;

-- --------------------------------------------------------------- admin ----

-- Bump the app version. Publish the GitHub release first, then call this.
create or replace function public.admin_set_clothchange_version(
  p_version text,
  p_min_version text default null
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
begin
  if public.loader_version_parts(trim(coalesce(p_version, ''))) is null then
    return jsonb_build_object('ok', false, 'error', 'invalid_version');
  end if;
  if p_min_version is not null and public.loader_version_parts(trim(p_min_version)) is null then
    return jsonb_build_object('ok', false, 'error', 'invalid_min_version');
  end if;

  update public.clothchange_config
  set version = trim(p_version),
      min_version = case when p_min_version is null then min_version else trim(p_min_version) end,
      updated_at = now()
  where id = 1;

  return jsonb_build_object('ok', true, 'version', trim(p_version));
end;
$$;

-- Set or clear the force-update floor. Null clears it (no hard block).
create or replace function public.admin_set_clothchange_min_version(p_min_version text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
begin
  if p_min_version is not null and public.loader_version_parts(trim(p_min_version)) is null then
    return jsonb_build_object('ok', false, 'error', 'invalid_min_version');
  end if;

  update public.clothchange_config
  set min_version = case when p_min_version is null then null else trim(p_min_version) end,
      updated_at = now()
  where id = 1;

  return jsonb_build_object('ok', true, 'min_version', coalesce(trim(p_min_version), ''));
end;
$$;

create or replace function public.admin_set_clothchange_maintenance(
  p_maintenance boolean,
  p_message text default ''
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
begin
  update public.clothchange_config
  set maintenance = coalesce(p_maintenance, false),
      maintenance_message = coalesce(p_message, ''),
      updated_at = now()
  where id = 1;

  return jsonb_build_object('ok', true, 'maintenance', coalesce(p_maintenance, false));
end;
$$;

create or replace function public.admin_set_clothchange_notice(
  p_notice text,
  p_level text default 'info'
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
begin
  if coalesce(p_level, 'info') not in ('info', 'warning', 'danger') then
    return jsonb_build_object('ok', false, 'error', 'invalid_level');
  end if;

  update public.clothchange_config
  set notice = coalesce(p_notice, ''),
      notice_level = coalesce(p_level, 'info'),
      updated_at = now()
  where id = 1;

  return jsonb_build_object('ok', true, 'notice_level', coalesce(p_level, 'info'));
end;
$$;

-- Point the self-update at a published release. Empty url clears it.
create or replace function public.admin_set_clothchange_download(
  p_download_url text,
  p_sha256 text
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_url text := trim(coalesce(p_download_url, ''));
  v_sha text := lower(trim(coalesce(p_sha256, '')));
begin
  if v_url <> '' and (v_url !~ '^https://' or v_sha !~ '^[0-9a-f]{64}$') then
    return jsonb_build_object('ok', false, 'error', 'invalid_download');
  end if;

  update public.clothchange_config
  set download_url = v_url,
      sha256 = case when v_url = '' then '' else v_sha end,
      updated_at = now()
  where id = 1;

  return jsonb_build_object('ok', true, 'download_url', v_url);
end;
$$;

create or replace function public.admin_set_clothchange_feature(
  p_key text,
  p_enabled boolean,
  p_description text default ''
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_key text := lower(trim(coalesce(p_key, '')));
begin
  if v_key = '' or length(v_key) > 40 then
    return jsonb_build_object('ok', false, 'error', 'invalid_key');
  end if;

  insert into public.clothchange_features (key, enabled, description)
  values (v_key, coalesce(p_enabled, false), coalesce(p_description, ''))
  on conflict (key) do update set
    enabled = excluded.enabled,
    description = case when excluded.description = '' then clothchange_features.description
                       else excluded.description end;

  return jsonb_build_object('ok', true, 'key', v_key, 'enabled', coalesce(p_enabled, false));
end;
$$;

create or replace function public.admin_remove_clothchange_feature(p_key text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_count integer;
begin
  delete from public.clothchange_features where key = lower(trim(coalesce(p_key, '')));
  get diagnostics v_count = row_count;

  return jsonb_build_object('ok', v_count > 0, 'deleted', v_count);
end;
$$;

-- Create or update one catalog entry.
create or replace function public.admin_upsert_clothchange_item(
  p_item_id bigint,
  p_name text,
  p_category text default 'other',
  p_rarity text default 'WHITE',
  p_icon text default '',
  p_enabled boolean default true,
  p_sort_order integer default 0
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_name text := trim(coalesce(p_name, ''));
  v_category text := lower(trim(coalesce(p_category, 'other')));
begin
  if p_item_id is null or p_item_id <= 0 then
    return jsonb_build_object('ok', false, 'error', 'invalid_item_id');
  end if;
  if v_name = '' or length(v_name) > 80 then
    return jsonb_build_object('ok', false, 'error', 'invalid_name');
  end if;
  if v_category not in ('top', 'bottom', 'shoes', 'head', 'mask', 'facepaint', 'traje', 'other') then
    return jsonb_build_object('ok', false, 'error', 'invalid_category');
  end if;

  insert into public.clothchange_items (item_id, name, category, rarity, icon, enabled, sort_order)
  values (p_item_id, v_name, v_category, upper(trim(coalesce(p_rarity, 'WHITE'))),
          coalesce(p_icon, ''), coalesce(p_enabled, true), coalesce(p_sort_order, 0))
  on conflict (item_id) do update set
    name = excluded.name,
    category = excluded.category,
    rarity = excluded.rarity,
    icon = excluded.icon,
    enabled = excluded.enabled,
    sort_order = excluded.sort_order;

  return jsonb_build_object('ok', true, 'item_id', p_item_id);
end;
$$;

create or replace function public.admin_remove_clothchange_item(p_item_id bigint)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_count integer;
begin
  delete from public.clothchange_items where item_id = p_item_id;
  get diagnostics v_count = row_count;

  return jsonb_build_object('ok', v_count > 0, 'deleted', v_count);
end;
$$;

-- -------------------------------------------------------------- grants ----

alter table public.clothchange_config enable row level security;
alter table public.clothchange_features enable row level security;
alter table public.clothchange_items enable row level security;

revoke all on public.clothchange_config from anon, authenticated;
revoke all on public.clothchange_features from anon, authenticated;
revoke all on public.clothchange_items from anon, authenticated;
grant all on public.clothchange_config to service_role;
grant all on public.clothchange_features to service_role;
grant all on public.clothchange_items to service_role;

do $$
declare
  f record;
begin
  for f in
    select p.oid::regprocedure as sig
    from pg_proc p
    where p.pronamespace = 'public'::regnamespace
      and (p.proname like 'clothchange\_%' or p.proname like 'admin\_set_clothchange\_%'
           or p.proname like 'admin\_upsert\_clothchange\_%'
           or p.proname like 'admin\_remove\_clothchange\_%')
  loop
    execute format('revoke all on function %s from public, anon, authenticated', f.sig);
    execute format('grant execute on function %s to service_role', f.sig);
  end loop;
end;
$$;

grant execute on function public.clothchange_bootstrap(text, boolean) to authenticated, service_role;
grant execute on function public.clothchange_catalog(boolean) to authenticated, service_role;

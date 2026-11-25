-- DESCRIPTION: Verilator: VHDL nested interface views on a top entity and on an instance
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package view_pkg is
  type channel_t is record
    data  : std_logic_vector(7 downto 0);
    valid : std_logic;
    ready : std_logic;
  end record;

  view channel_master_view of channel_t is
    valid : out;
    data  : out;
    ready : in;
  end view channel_master_view;

  alias channel_slave_view is channel_master_view'converse;

  type bidir_t is record
    tx : channel_t;
    rx : channel_t;
  end record;

  view bidir_master_view of bidir_t is
    tx : view channel_master_view;
    rx : view channel_slave_view;
  end view;

  alias bidir_slave_view is bidir_master_view'converse;

  type channel_array_t is array (natural range <>) of channel_t;

  type bus_t is record
    tx : channel_array_t(1 to 2);
    rx : channel_t;
  end record;

  view bus_master_view of bus_t is
    tx : view (channel_master_view);
    rx : view channel_slave_view;
  end view;

  alias bus_slave_view is bus_master_view'converse;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.view_pkg.all;

-- Passes tx through to rx with the data inverted, the ready the other way
entity loop_back is
  port (b : view bidir_master_view);
end entity;

architecture rtl of loop_back is
begin
  b.tx.valid <= b.rx.valid;
  b.tx.data  <= not b.rx.data;
  b.rx.ready <= b.tx.ready;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.view_pkg.all;

entity t is
  port (
    m      : view bidir_master_view;
    a_valid : in  std_logic;
    a_data  : in  std_logic_vector(7 downto 0);
    a_ready : out std_logic;
    b_valid : out std_logic;
    b_data  : out std_logic_vector(7 downto 0);
    b_ready : in  std_logic;
    c_valid : out std_logic;
    c_data  : out std_logic_vector(7 downto 0);
    c_ready : out std_logic
  );
end entity;

architecture rtl of t is
  signal s, s2 : bidir_t;
begin
  -- The top entity's own view port: the same loop back, written inline
  m.tx.valid <= m.rx.valid;
  m.tx.data  <= not m.rx.data;
  m.rx.ready <= m.tx.ready;

  -- Whole-record association; s is the slave end seen from the entity
  u1 : entity work.loop_back port map (b => s);
  s.rx.valid <= a_valid;
  s.rx.data  <= a_data;
  a_ready    <= s.rx.ready;
  b_valid    <= s.tx.valid;
  b_data     <= s.tx.data;
  s.tx.ready <= b_ready;

  -- The same through element associations
  u2 : entity work.loop_back port map (b.tx => s2.tx, b.rx => s2.rx);
  s2.rx.valid <= a_valid;
  s2.rx.data  <= a_data;
  s2.tx.ready <= b_ready;
  c_valid     <= s2.tx.valid;
  c_data      <= s2.tx.data;
  c_ready     <= s2.rx.ready;
end architecture;

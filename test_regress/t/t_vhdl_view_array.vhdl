-- DESCRIPTION: Verilator: VHDL interface views of an array of records, on a top entity
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

entity merge is
  port (
    b   : view bus_slave_view;
    arr : view (channel_slave_view) of channel_array_t(2 downto 0)
  );
end entity;

architecture rtl of merge is
begin
  b.tx(1).ready <= b.rx.ready;
  b.tx(2).ready <= b.rx.ready;
  b.rx.valid    <= b.tx(1).valid and b.tx(2).valid;
  b.rx.data     <= b.tx(1).data xor b.tx(2).data;

  arr(0).ready <= '1';
  arr(1).ready <= arr(0).valid;
  arr(2).ready <= arr(1).valid and arr(2).valid;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.view_pkg.all;

entity t is
  port (
    b   : view bus_slave_view;
    arr : view (channel_slave_view) of channel_array_t(2 downto 0)
  );
end entity;

architecture rtl of t is
begin
  -- Whole-port associations of an array of views, one inside a record
  u : entity work.merge port map (b => b, arr => arr);
end architecture;

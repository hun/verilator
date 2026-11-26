-- DESCRIPTION: Verilator: VHDL open record-member association on an input record port is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package rp is type rec is record f : bit; g : bit; end record; end package rp;
use work.rp.all;
entity sub is port (r : in rec); end entity sub;
architecture rtl of sub is begin end architecture rtl;
entity t is port (b : in bit); end entity t;
architecture rtl of t is
begin
  u : entity work.sub port map (r.f => open, r.g => b);
end architecture rtl;
